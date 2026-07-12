#include "demi/runtime/input/InputActionParser.h"

#include "demi/runtime/scene/SceneJson.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace demi::runtime::input {
namespace {

using Json = nlohmann::json;

std::string normalized(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

void parseBinding(const Json &value, InputAction &action) {
  if (value.is_string()) {
    action.bindings.push_back({.input = normalized(value.get<std::string>())});
    return;
  }
  if (!value.is_object())
    return;
  std::string input = scene_loading::stringOr(
      value, "input", scene_loading::stringOr(value, "key"));
  if (input.empty())
    return;
  action.bindings.push_back(
      {.input = normalized(std::move(input)),
       .scale = scene_loading::numberField(value, "scale").value_or(1.0F)});
}

} // namespace

InputActionMap parseInputActions(const nlohmann::json &projectDocument) {
  InputActionMap result;
  const Json *input = scene_loading::objectField(projectDocument, "input");
  const Json *actions = input == nullptr
                            ? nullptr
                            : scene_loading::objectField(*input, "actions");
  if (actions == nullptr)
    return result;

  for (const auto &[name, definition] : actions->items()) {
    InputAction action;
    const Json *bindings =
        definition.is_array()
            ? &definition
            : scene_loading::arrayField(definition, "bindings");
    if (bindings != nullptr)
      for (const Json &binding : *bindings)
        parseBinding(binding, action);
    if (!action.bindings.empty())
      result[normalized(name)] = std::move(action);
  }
  return result;
}

} // namespace demi::runtime::input
