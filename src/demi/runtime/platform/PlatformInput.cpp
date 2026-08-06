#include "demi/runtime/platform/PlatformInput.h"

#include <algorithm>

namespace demi::runtime::platform {

PointerMotion pointerMotionInDrawablePixels(const PointerMotion motion,
                                            const Vec2 windowSize,
                                            const Vec2 drawableSize) {
  const float scaleX =
      std::max(drawableSize.x, 1.0F) / std::max(windowSize.x, 1.0F);
  const float scaleY =
      std::max(drawableSize.y, 1.0F) / std::max(windowSize.y, 1.0F);
  return {.position = {motion.position.x * scaleX, motion.position.y * scaleY},
          .delta = {motion.delta.x * scaleX, motion.delta.y * scaleY}};
}

namespace {

void setDigital(std::unordered_set<std::string> &down,
                std::unordered_set<std::string> &pressed,
                std::unordered_set<std::string> &released,
                const std::string &name, const bool value,
                const bool repeat = false) {
  const bool wasDown = down.contains(name);
  if (value) {
    down.insert(name);
    released.erase(name);
    if (!wasDown && !repeat)
      pressed.insert(name);
  } else {
    down.erase(name);
    pressed.erase(name);
    if (wasDown)
      released.insert(name);
  }
}

} // namespace

PlatformInput::PlatformInput(InputState &state) : state_(state) {}

void PlatformInput::beginFrame() {
  state_.keysPressed.clear();
  state_.keysReleased.clear();
  state_.mouseButtonsPressed.clear();
  state_.mouseButtonsReleased.clear();
  state_.mouseDelta = {};
  state_.textEntered.clear();
  state_.textCompositionChanged = false;
  std::erase_if(state_.touches, [](const TouchPoint &touch) {
    return touch.phase == TouchPhase::Ended ||
           touch.phase == TouchPhase::Cancelled;
  });
  for (TouchPoint &touch : state_.touches) {
    touch.phase = TouchPhase::Stationary;
    touch.delta = {};
  }
  for (GamepadState &gamepad : state_.gamepads) {
    gamepad.buttonsPressed.clear();
    gamepad.buttonsReleased.clear();
  }
}

void PlatformInput::key(const std::string_view name, const bool down,
                        const bool repeat) {
  setDigital(state_.keysDown, state_.keysPressed, state_.keysReleased,
             std::string(name), down, repeat);
}

void PlatformInput::text(const std::string_view utf8) {
  state_.textEntered.append(utf8);
  if (!state_.textComposition.empty()) {
    state_.textComposition.clear();
    state_.textCompositionSelectionStart = 0;
    state_.textCompositionSelectionLength = 0;
    state_.textCompositionChanged = true;
  }
}

void PlatformInput::composition(const std::string_view utf8,
                                const int selectionStart,
                                const int selectionLength) {
  state_.textComposition = utf8;
  state_.textCompositionSelectionStart =
      static_cast<std::size_t>(std::max(selectionStart, 0));
  state_.textCompositionSelectionLength =
      static_cast<std::size_t>(std::max(selectionLength, 0));
  state_.textCompositionChanged = true;
}

void PlatformInput::pointerPosition(const float x, const float y,
                                    const float deltaX, const float deltaY) {
  state_.mousePosition = {x, y};
  state_.mouseDelta.x += deltaX;
  state_.mouseDelta.y += deltaY;
}

void PlatformInput::pointerButton(const std::string_view name,
                                  const bool down) {
  setDigital(state_.mouseButtonsDown, state_.mouseButtonsPressed,
             state_.mouseButtonsReleased, std::string(name), down);
}

void PlatformInput::touch(const std::int64_t id, const TouchPhase phase,
                          const Vec2 position, const Vec2 delta,
                          const float pressure) {
  const auto existing =
      std::ranges::find(state_.touches, id, &TouchPoint::id);
  const TouchPoint value{.id = id,
                         .phase = phase,
                         .position = position,
                         .delta = delta,
                         .pressure = pressure};
  if (existing == state_.touches.end())
    state_.touches.push_back(value);
  else
    *existing = value;
}

void PlatformInput::connectGamepad(const int deviceId, std::string name) {
  GamepadState &state = gamepad(deviceId);
  state.name = std::move(name);
}

void PlatformInput::disconnectGamepad(const int deviceId) {
  std::erase_if(state_.gamepads, [deviceId](const GamepadState &gamepad) {
    return gamepad.deviceId == deviceId;
  });
}

void PlatformInput::gamepadButton(const int deviceId,
                                  const std::string_view name,
                                  const bool down) {
  GamepadState &state = gamepad(deviceId);
  setDigital(state.buttonsDown, state.buttonsPressed, state.buttonsReleased,
             std::string(name), down);
  if (name == "dpad_down")
    mirrorUiButton(state, name, "ui_next", down);
  else if (name == "dpad_up")
    mirrorUiButton(state, name, "ui_previous", down);
  else if (name == "south")
    mirrorUiButton(state, name, "ui_accept", down);
}

void PlatformInput::gamepadAxis(const int deviceId,
                                const std::string_view name,
                                const float value) {
  gamepad(deviceId).axes[std::string(name)] =
      std::clamp(value, -1.0F, 1.0F);
}

GamepadState &PlatformInput::gamepad(const int deviceId) {
  const auto existing =
      std::ranges::find(state_.gamepads, deviceId, &GamepadState::deviceId);
  if (existing != state_.gamepads.end())
    return *existing;
  const int player = state_.gamepadAssignments.contains(deviceId)
                         ? state_.gamepadAssignments.at(deviceId)
                         : deviceId;
  GamepadState added;
  added.deviceId = deviceId;
  added.player = player;
  return state_.gamepads.emplace_back(std::move(added));
}

void PlatformInput::mirrorUiButton(GamepadState &, const std::string_view,
                                   const std::string_view key,
                                   const bool down) {
  setDigital(state_.keysDown, state_.keysPressed, state_.keysReleased,
             std::string(key), down);
}

} // namespace demi::runtime::platform
