#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace demi::runtime::platform {

struct PointerMotion {
  Vec2 position;
  Vec2 delta;
};

// Window systems report pointer coordinates in window points, while render
// backends consume drawable pixels on high-density displays. Normalize at the
// platform boundary so game-facing input and rendering share one space.
[[nodiscard]] PointerMotion pointerMotionInDrawablePixels(PointerMotion motion,
                                                          Vec2 windowSize,
                                                          Vec2 drawableSize);

// Owns platform-independent input state transitions. Platform adapters only
// translate native events into these operations; gameplay reads InputState.
class PlatformInput {
public:
  explicit PlatformInput(InputState &state);

  void beginFrame();
  void key(std::string_view name, bool down, bool repeat = false);
  void text(std::string_view utf8);
  void composition(std::string_view utf8, int selectionStart,
                   int selectionLength);
  void pointerPosition(float x, float y, float deltaX, float deltaY);
  void pointerButton(std::string_view name, bool down);
  void pointerScroll(float x, float y);
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
