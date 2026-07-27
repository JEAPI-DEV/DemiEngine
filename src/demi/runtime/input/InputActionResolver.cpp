#include "demi/runtime/input/InputActionResolver.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

namespace demi::runtime::input {
namespace {

std::string normalized(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

const InputAction *findAction(const InputActionMap &actions,
                              const std::string_view name) {
  const auto found = actions.find(normalized(std::string(name)));
  return found == actions.end() ? nullptr : &found->second;
}

struct BindingState {
  bool held = false;
  bool pressed = false;
  bool released = false;
  Vec2 value;
  std::string source;
};

float processedAxis(float value, const InputBinding &binding) {
  if (std::abs(value) <= std::clamp(binding.deadzone, 0.0F, 0.99F))
    return 0.0F;
  value = binding.invert ? -value : value;
  return std::clamp(value * binding.scale, -1.0F, 1.0F);
}

const GamepadState *gamepadFor(const InputState &state, const int player,
                               const int bindingPlayer) {
  const int wanted = bindingPlayer >= 0 ? bindingPlayer : player;
  const auto found = std::ranges::find_if(
      state.gamepads, [&](const GamepadState &gamepad) {
        return wanted < 0 || gamepad.player == wanted;
      });
  return found == state.gamepads.end() ? nullptr : &*found;
}

BindingState resolveBinding(const InputState &state,
                            const InputBinding &binding, const int player) {
  BindingState result;
  const std::string input = normalized(binding.input);
  result.source = input;

  const auto digital = [&](const auto &down, const auto &pressed,
                           const auto &released, const std::string &name) {
    result.held = down.contains(name);
    result.pressed = pressed.contains(name);
    result.released = released.contains(name);
    result.value = {.x = result.held ? binding.scale : 0.0F};
  };

  if (input.starts_with("key:")) {
    digital(state.keysDown, state.keysPressed, state.keysReleased,
            input.substr(4));
  } else if (input.starts_with("mouse:")) {
    digital(state.mouseButtonsDown, state.mouseButtonsPressed,
            state.mouseButtonsReleased, input.substr(6));
  } else if (input.starts_with("virtual:")) {
    std::string name = input.substr(8);
    char component = '\0';
    if (name.ends_with(":x") || name.ends_with(":y")) {
      component = name.back();
      name.resize(name.size() - 2);
    }
    if (const auto axis = state.virtualAxes.find(name);
        axis != state.virtualAxes.end()) {
      if (component == 'x')
        result.value.x = processedAxis(axis->second.x, binding);
      else if (component == 'y')
        result.value.x = processedAxis(axis->second.y, binding);
      else
        result.value = {.x = processedAxis(axis->second.x, binding),
                        .y = processedAxis(axis->second.y, binding)};
      result.held = std::abs(result.value.x) > 0.0001F ||
                    std::abs(result.value.y) > 0.0001F;
    } else {
      digital(state.virtualButtonsDown, state.virtualButtonsPressed,
              state.virtualButtonsReleased, name);
    }
  } else if (input.starts_with("gamepad:")) {
    const GamepadState *gamepad = gamepadFor(state, player, binding.player);
    if (gamepad == nullptr)
      return result;
    std::string control = input.substr(8);
    result.source =
        "gamepad:" + std::to_string(gamepad->deviceId) + ":" + control;
    if (control.starts_with("button:"))
      control.erase(0, 7);
    if (control.starts_with("axis:")) {
      control.erase(0, 5);
      const auto axis = gamepad->axes.find(control);
      result.value.x =
          processedAxis(axis == gamepad->axes.end() ? 0.0F : axis->second,
                        binding);
      result.held = std::abs(result.value.x) > 0.0001F;
    } else if (control == "stick:left" || control == "stick:right") {
      const std::string prefix =
          control == "stick:left" ? "left_" : "right_";
      const float x = gamepad->axes.contains(prefix + "x")
                          ? gamepad->axes.at(prefix + "x")
                          : 0.0F;
      const float y = gamepad->axes.contains(prefix + "y")
                          ? gamepad->axes.at(prefix + "y")
                          : 0.0F;
      result.value = {.x = processedAxis(x, binding),
                      .y = processedAxis(y, binding)};
      const float length =
          std::sqrt(result.value.x * result.value.x +
                    result.value.y * result.value.y);
      if (binding.normalize && length > 1.0F) {
        result.value.x /= length;
        result.value.y /= length;
      }
      result.held = length > 0.0001F;
    } else {
      digital(gamepad->buttonsDown, gamepad->buttonsPressed,
              gamepad->buttonsReleased, control);
    }
  } else {
    // Legacy project bindings such as "space" remain keyboard bindings.
    digital(state.keysDown, state.keysPressed, state.keysReleased, input);
  }

  if ((binding.x != 0.0F || binding.y != 0.0F) && result.held) {
    result.value = {.x = binding.x * binding.scale,
                    .y = binding.y * binding.scale};
    if (binding.invert) {
      result.value.x = -result.value.x;
      result.value.y = -result.value.y;
    }
  }
  return result;
}

} // namespace

InputActionState InputActionResolver::resolve(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  const std::string actionName = normalized(std::string(action));
  if (const auto recorded = state.recordedActions.find(actionName);
      recorded != state.recordedActions.end() &&
      (player < 0 || recorded->second.player < 0 ||
       player == recorded->second.player)) {
    return {.held = recorded->second.held,
            .pressed = recorded->second.pressed,
            .released = recorded->second.released,
            .value = recorded->second.value,
            .vector = recorded->second.vector,
            .source = recorded->second.source};
  }
  InputActionState result;
  const InputAction *definition = findAction(actions, action);
  if (definition == nullptr ||
      (contexts != nullptr && !contexts->empty() &&
       !contexts->contains(definition->context)) ||
      (definition->player >= 0 && player >= 0 &&
       definition->player != player))
    return result;

  for (const InputBinding &binding : definition->bindings) {
    const BindingState current = resolveBinding(state, binding, player);
    result.held = result.held || current.held;
    result.pressed = result.pressed || current.pressed;
    result.released = result.released || current.released;
    result.vector.x += current.value.x;
    result.vector.y += current.value.y;
    if (result.source.empty() &&
        (current.held || current.pressed || current.released))
      result.source = current.source;
  }
  result.vector.x = std::clamp(result.vector.x, -1.0F, 1.0F);
  result.vector.y = std::clamp(result.vector.y, -1.0F, 1.0F);
  if (definition->type == InputActionType::Button)
    result.value = result.held ? 1.0F : 0.0F;
  else
    result.value = result.vector.x;
  return result;
}

bool InputActionResolver::down(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  return resolve(actions, state, action, player, contexts).held;
}

bool InputActionResolver::pressed(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  return resolve(actions, state, action, player, contexts).pressed;
}

bool InputActionResolver::released(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  return resolve(actions, state, action, player, contexts).released;
}

float InputActionResolver::value(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  return resolve(actions, state, action, player, contexts).value;
}

Vec2 InputActionResolver::vector(
    const InputActionMap &actions, const InputState &state,
    const std::string_view action, const int player,
    const std::unordered_set<std::string> *contexts) const {
  return resolve(actions, state, action, player, contexts).vector;
}

} // namespace demi::runtime::input
