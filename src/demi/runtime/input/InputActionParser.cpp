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
  if (!value.is_object())
    return;
  std::string input = scene_loading::stringOr(value, "input");
  if (input.empty() || input.find(':') == std::string::npos)
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
  if (type == "axis1d")
    return InputActionType::Axis1D;
  if (type == "vector2")
    return InputActionType::Vector2;
  return InputActionType::Button;
}

} // namespace

InputActionMap parseInputActions(const nlohmann::json &projectDocument) {
  InputActionMap result;
  const Json *input = scene_loading::objectField(projectDocument, "input");
  if (input == nullptr)
    return result;

  // P8: named input presets expanded before explicit actions.
  // Explicit actions merge over (replace) preset actions with the same name.
  if (const Json *presets = scene_loading::arrayField(*input, "presets")) {
    for (const Json &preset : *presets) {
      if (!preset.is_string())
        continue;
      const std::string name = normalized(preset.get<std::string>());
      if (name == "wasd_arrows") {
        InputAction moveX{.type = InputActionType::Axis1D,
                          .context = "gameplay"};
        moveX.bindings = {{.input = "key:a", .scale = -1.0F},
                          {.input = "key:left", .scale = -1.0F},
                          {.input = "key:d", .scale = 1.0F},
                          {.input = "key:right", .scale = 1.0F}};
        InputAction moveY{.type = InputActionType::Axis1D,
                          .context = "gameplay"};
        moveY.bindings = {{.input = "key:s", .scale = -1.0F},
                          {.input = "key:down", .scale = -1.0F},
                          {.input = "key:w", .scale = 1.0F},
                          {.input = "key:up", .scale = 1.0F}};
        result.emplace("move_x", moveX);
        result.emplace("move_y", moveY);
      } else if (name == "confirm" || name == "gamepad_confirm") {
        InputAction confirm{.type = InputActionType::Button,
                            .context = "gameplay"};
        confirm.bindings = {{.input = "key:space"},
                            {.input = "key:enter"},
                            {.input = "gamepad:south"}};
        result.emplace("confirm", confirm);
      } else if (name == "move_3d") {
        InputAction right{.type = InputActionType::Axis1D,
                          .context = "gameplay"};
        right.bindings = {{.input = "key:a", .scale = -1.0F},
                          {.input = "key:d", .scale = 1.0F}};
        InputAction forward{.type = InputActionType::Axis1D,
                            .context = "gameplay"};
        forward.bindings = {{.input = "key:s", .scale = -1.0F},
                            {.input = "key:w", .scale = 1.0F}};
        result.emplace("move_right", right);
        result.emplace("move_forward", forward);
      }
      // Unknown preset names are ignored here; validation reports them.
    }
  }

  const Json *actions = scene_loading::objectField(*input, "actions");
  if (actions == nullptr)
    return result;

  for (const auto &[name, definition] : actions->items()) {
    InputAction action;
    if (!definition.is_object() || !definition.contains("type") ||
        !definition.contains("context"))
      continue;
    const std::string typeName =
        normalized(scene_loading::stringOr(definition, "type"));
    if (typeName != "button" && typeName != "axis1d" &&
        typeName != "vector2")
      continue;
    action.type = actionType(definition);
    action.context = normalized(scene_loading::stringOr(definition, "context"));
    action.player = definition.value("player", -1);
    const Json *bindings = scene_loading::arrayField(definition, "bindings");
    if (bindings != nullptr)
      for (const Json &binding : *bindings)
        parseBinding(binding, action);
    if (!action.bindings.empty())
      result[normalized(name)] = std::move(action);
  }
  return result;
}

std::vector<std::string> knownInputPresets() {
  return {"wasd_arrows", "confirm", "gamepad_confirm", "move_3d"};
}

} // namespace demi::runtime::input
