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
  InputBinding binding{
      .input = normalized(std::move(input)),
      .scale = scene_loading::numberField(value, "scale").value_or(1.0F),
      .deadzone =
          scene_loading::numberField(value, "deadzone").value_or(0.0F),
      .invert = value.value("invert", false),
      .normalize = value.value("normalize", false),
      .player = value.value("player", -1),
  };
  if (const Json *vector = scene_loading::arrayField(value, "vector");
      vector != nullptr && vector->size() == 2 && (*vector)[0].is_number() &&
      (*vector)[1].is_number()) {
    binding.x = (*vector)[0].get<float>();
    binding.y = (*vector)[1].get<float>();
  }
  action.bindings.push_back(std::move(binding));
}

InputActionType actionType(const Json &definition) {
  const std::string type =
      normalized(scene_loading::stringOr(definition, "type", "button"));
  if (type == "axis" || type == "axis1d" || type == "1d")
    return InputActionType::Axis1D;
  if (type == "vector" || type == "vector2" || type == "2d")
    return InputActionType::Vector2;
  return InputActionType::Button;
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
    if (definition.is_object()) {
      action.type = actionType(definition);
      action.context =
          normalized(scene_loading::stringOr(definition, "context", "gameplay"));
      action.player = definition.value("player", -1);
    }
    const Json *bindings =
        definition.is_array()
            ? &definition
            : scene_loading::arrayField(definition, "bindings");
    if (bindings != nullptr)
      for (const Json &binding : *bindings)
        parseBinding(binding, action);
    if ((!definition.is_object() || !definition.contains("type")) &&
        std::ranges::any_of(action.bindings, [](const InputBinding &binding) {
          return binding.scale != 1.0F;
        }))
      action.type = InputActionType::Axis1D;
    if (!action.bindings.empty())
      result[normalized(name)] = std::move(action);
  }
  return result;
}

} // namespace demi::runtime::input
