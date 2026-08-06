#include "demi/runtime/input/InputRebinding.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::runtime::input {
namespace {

std::string normalized(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

nlohmann::json bindingJson(const InputBinding &binding) {
  nlohmann::json result{{"input", binding.input}, {"scale", binding.scale}};
  if (binding.x != 0.0F || binding.y != 0.0F)
    result["vector"] = {binding.x, binding.y};
  if (binding.deadzone != 0.0F)
    result["deadzone"] = binding.deadzone;
  if (binding.invert)
    result["invert"] = true;
  if (binding.normalize)
    result["normalize"] = true;
  if (binding.player >= 0)
    result["player"] = binding.player;
  return result;
}

InputBinding readBinding(const nlohmann::json &value) {
  InputBinding result;
  result.input = normalized(value.value("input", std::string{}));
  result.scale = value.value("scale", 1.0F);
  result.deadzone = std::clamp(value.value("deadzone", 0.0F), 0.0F, 0.99F);
  result.invert = value.value("invert", false);
  result.normalize = value.value("normalize", false);
  result.player = value.value("player", -1);
  if (value.contains("vector") && value["vector"].is_array() &&
      value["vector"].size() == 2) {
    result.x = value["vector"][0].get<float>();
    result.y = value["vector"][1].get<float>();
  }
  return result;
}

} // namespace

bool InputRebinding::rebind(InputActionMap &actions, const std::string &action,
                            const std::size_t bindingIndex,
                            InputBinding binding, std::string &error) {
  const auto found = actions.find(normalized(action));
  if (found == actions.end()) {
    error = "unknown input action '" + action + "'";
    return false;
  }
  if (binding.input.empty()) {
    error = "input binding cannot be empty";
    return false;
  }
  if (bindingIndex >= found->second.bindings.size()) {
    error = "binding index is out of range for action '" + action + "'";
    return false;
  }
  binding.input = normalized(std::move(binding.input));
  binding.deadzone = std::clamp(binding.deadzone, 0.0F, 0.99F);
  found->second.bindings[bindingIndex] = std::move(binding);
  error.clear();
  return true;
}

bool InputRebinding::save(const InputActionMap &actions,
                          const std::filesystem::path &path,
                          std::string &error) {
  try {
    if (!path.parent_path().empty())
      std::filesystem::create_directories(path.parent_path());
    nlohmann::json document{
        {"format_version", 1}, {"bindings", nlohmann::json::object()}};
    for (const auto &[name, action] : actions) {
      nlohmann::json values = nlohmann::json::array();
      for (const InputBinding &binding : action.bindings)
        values.push_back(bindingJson(binding));
      document["bindings"][name] = std::move(values);
    }
    std::ofstream output(path);
    if (!output) {
      error = "could not write input bindings: " + path.string();
      return false;
    }
    output << document.dump(2) << '\n';
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error = "could not save input bindings: " + std::string(exception.what());
    return false;
  }
}

bool InputRebinding::load(InputActionMap &actions,
                          const std::filesystem::path &path,
                          std::string &error) {
  try {
    std::ifstream input(path);
    if (!input) {
      error = "could not open input bindings: " + path.string();
      return false;
    }
    const nlohmann::json document = nlohmann::json::parse(input);
    if (document.value("format_version", 0) != 1 ||
        !document.contains("bindings") || !document["bindings"].is_object()) {
      error = "input bindings require format_version 1 and bindings";
      return false;
    }
    InputActionMap candidate = actions;
    for (const auto &[name, values] : document["bindings"].items()) {
      const auto found = candidate.find(normalized(name));
      if (found == candidate.end() || !values.is_array())
        continue;
      std::vector<InputBinding> bindings;
      for (const auto &value : values) {
        if (!value.is_object()) {
          error = "binding override for '" + name + "' must be an object";
          return false;
        }
        InputBinding binding = readBinding(value);
        if (binding.input.empty()) {
          error = "binding override for '" + name + "' has no input";
          return false;
        }
        bindings.push_back(std::move(binding));
      }
      if (!bindings.empty())
        found->second.bindings = std::move(bindings);
    }
    actions = std::move(candidate);
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error = "invalid input bindings: " + std::string(exception.what());
    return false;
  }
}

} // namespace demi::runtime::input
