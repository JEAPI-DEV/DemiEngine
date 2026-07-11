#include "demi/runtime/scripting/annotations/LuaAnnotationScanner.h"

#include <fstream>
#include <optional>

namespace demi::runtime {
namespace {

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::optional<std::string> annotationValue(const std::string &line,
                                           std::string_view marker) {
  const std::size_t markerPosition = line.find(marker);
  if (markerPosition == std::string::npos)
    return std::nullopt;
  const std::size_t firstQuote = line.find('"', markerPosition + marker.size());
  if (firstQuote == std::string::npos)
    return std::nullopt;
  const std::size_t secondQuote = line.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos)
    return std::nullopt;
  return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::string> functionName(const std::string &line) {
  constexpr std::string_view prefix = "function ";
  std::string text = trim(line);
  if (!text.starts_with(prefix))
    return std::nullopt;
  const std::size_t parenthesis = text.find('(', prefix.size());
  if (parenthesis == std::string::npos)
    return std::nullopt;
  std::string name =
      trim(text.substr(prefix.size(), parenthesis - prefix.size()));
  if (const std::size_t separator = name.find_last_of(".:");
      separator != std::string::npos)
    name = name.substr(separator + 1);
  return name.empty() ? std::nullopt
                      : std::optional<std::string>{std::move(name)};
}

} // namespace

std::vector<LuaAnnotatedFunction>
LuaAnnotationScanner::scan(const std::filesystem::path &path,
                           std::string_view marker) {
  std::vector<LuaAnnotatedFunction> annotations;
  std::ifstream input(path);
  if (!input)
    return annotations;

  std::vector<std::string> pendingValues;
  std::string line;
  while (std::getline(input, line)) {
    if (auto value = annotationValue(line, marker)) {
      pendingValues.push_back(std::move(*value));
      continue;
    }
    if (pendingValues.empty())
      continue;
    const std::string text = trim(line);
    if (text.empty() || text.starts_with("--"))
      continue;
    if (auto name = functionName(text)) {
      for (std::string &value : pendingValues)
        annotations.push_back(
            {.value = std::move(value), .functionName = *name});
    }
    pendingValues.clear();
  }
  return annotations;
}

} // namespace demi::runtime
