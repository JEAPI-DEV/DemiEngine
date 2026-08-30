#include "demi/runtime/ui/UiVariables.h"

#include "demi/runtime/ui/TextEditingEngine.h"

namespace demi::runtime::ui {
namespace {

void discoverIn(const std::string_view text,
                std::unordered_set<std::string> &variables) {
  std::size_t cursor = 0;
  while ((cursor = text.find("${", cursor)) != std::string_view::npos) {
    const std::size_t end = text.find('}', cursor + 2);
    if (end == std::string_view::npos)
      return;
    if (end > cursor + 2)
      variables.emplace(text.substr(cursor + 2, end - cursor - 2));
    cursor = end + 1;
  }
}

} // namespace

std::string UiVariables::resolve(
    const std::string_view text,
    const std::unordered_map<std::string, std::string> &variables) {
  std::string result;
  result.reserve(text.size());
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const std::size_t begin = text.find("${", cursor);
    if (begin == std::string_view::npos) {
      result.append(text.substr(cursor));
      break;
    }
    result.append(text.substr(cursor, begin - cursor));
    const std::size_t end = text.find('}', begin + 2);
    if (end == std::string_view::npos) {
      result.append(text.substr(begin));
      break;
    }
    const std::string name(text.substr(begin + 2, end - begin - 2));
    const auto variable = variables.find(name);
    if (variable == variables.end())
      result.append(text.substr(begin, end - begin + 1));
    else
      result += variable->second;
    cursor = end + 1;
  }
  return result;
}

bool UiVariables::set(UiDocument &document, std::string name, std::string value,
                      std::string &error) const {
  if (!document.declaredVariables.contains(name)) {
    error = "HUD variable is not declared: " + name;
    return false;
  }
  document.variables[std::move(name)] = std::move(value);
  apply(document);
  error.clear();
  return true;
}

bool UiVariables::setMany(
    UiDocument &document,
    std::unordered_map<std::string, std::string> variables,
    std::string &error) const {
  for (const auto &[name, value] : variables) {
    static_cast<void>(value);
    if (!document.declaredVariables.contains(name)) {
      error = "HUD variable is not declared: " + name;
      return false;
    }
  }
  document.variables = std::move(variables);
  apply(document);
  error.clear();
  return true;
}

void UiVariables::reset(UiDocument &document) const {
  document.variables.clear();
  apply(document);
}

void UiVariables::discover(UiDocument &document) const {
  for (const UiNode &node : document.nodes) {
    discoverIn(node.textTemplate, document.declaredVariables);
    discoverIn(node.placeholderTemplate, document.declaredVariables);
  }
}

void UiVariables::apply(UiDocument &document) const {
  for (UiNode &node : document.nodes) {
    std::string_view text = node.textTemplate;
    if (!node.localizationKey.empty()) {
      const auto localized = document.localization.find(node.localizationKey);
      if (localized != document.localization.end())
        text = localized->second;
    }
    if (text.find("${") != std::string_view::npos) {
      node.text = resolve(text, document.variables);
      TextEditingEngine::clearComposition(node.textEdit);
      TextEditingEngine::normalize(node.text, node.textEdit);
    }

    std::string_view placeholder = node.placeholderTemplate;
    if (!node.placeholderLocalizationKey.empty()) {
      const auto localized =
          document.localization.find(node.placeholderLocalizationKey);
      if (localized != document.localization.end())
        placeholder = localized->second;
    }
    if (placeholder.find("${") != std::string_view::npos)
      node.placeholder = resolve(placeholder, document.variables);
  }
}

} // namespace demi::runtime::ui
