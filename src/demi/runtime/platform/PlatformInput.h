#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace demi::runtime::platform {

// Owns platform-independent input state transitions. Platform adapters only
// translate native events into these operations; gameplay reads InputState.
class PlatformInput {
public:
  explicit PlatformInput(InputState &state);

  void beginFrame();
  void key(std::string_view name, bool down, bool repeat = false);
  void text(std::string_view utf8);
  void pointerPosition(float x, float y, float deltaX, float deltaY);
  void pointerButton(std::string_view name, bool down);
  void touch(std::int64_t id, TouchPhase phase, Vec2 position, Vec2 delta,
             float pressure);
  void connectGamepad(int deviceId, std::string name);
  void disconnectGamepad(int deviceId);
  void gamepadButton(int deviceId, std::string_view name, bool down);
  void gamepadAxis(int deviceId, std::string_view name, float value);

private:
  [[nodiscard]] GamepadState &gamepad(int deviceId);
  void mirrorUiButton(GamepadState &gamepad, std::string_view button,
                      std::string_view key, bool down);

  InputState &state_;
};

} // namespace demi::runtime::platform
