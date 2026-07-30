#include "demi/runtime/platform/PlatformInput.h"

#include <cassert>
#include <cmath>

using demi::runtime::InputState;
using demi::runtime::TouchPhase;
using demi::runtime::Vec2;
using demi::runtime::platform::PlatformInput;

namespace {

void keyboardTransitionsIgnoreRepeats() {
  InputState state;
  PlatformInput input(state);

  input.beginFrame();
  input.key("space", true);
  assert(state.keysDown.contains("space"));
  assert(state.keysPressed.contains("space"));
  assert(state.keysReleased.empty());

  input.beginFrame();
  input.key("space", true, true);
  assert(state.keysDown.contains("space"));
  assert(state.keysPressed.empty());

  input.key("space", false);
  assert(!state.keysDown.contains("space"));
  assert(state.keysReleased.contains("space"));

  input.beginFrame();
  input.key("space", false);
  assert(state.keysReleased.empty());
}

void pointerDeltaAccumulatesWithinFrame() {
  InputState state;
  PlatformInput input(state);
  input.beginFrame();
  input.pointerPosition(10.0F, 20.0F, 2.0F, 3.0F);
  input.pointerPosition(14.0F, 18.0F, 4.0F, -2.0F);
  assert(state.mousePosition.x == 14.0F);
  assert(state.mousePosition.y == 18.0F);
  assert(state.mouseDelta.x == 6.0F);
  assert(state.mouseDelta.y == 1.0F);
  input.beginFrame();
  assert(state.mouseDelta.x == 0.0F);
  assert(state.mouseDelta.y == 0.0F);
}

void touchTerminalStateSurvivesOneFrame() {
  InputState state;
  PlatformInput input(state);
  input.beginFrame();
  input.touch(7, TouchPhase::Began, Vec2{10.0F, 12.0F}, {}, 0.5F);
  assert(state.touches.size() == 1);
  assert(state.touches.front().phase == TouchPhase::Began);

  input.beginFrame();
  assert(state.touches.front().phase == TouchPhase::Stationary);
  input.touch(7, TouchPhase::Ended, Vec2{11.0F, 12.0F},
              Vec2{1.0F, 0.0F}, 0.0F);
  assert(state.touches.front().phase == TouchPhase::Ended);

  input.beginFrame();
  assert(state.touches.empty());
}

void gamepadDisconnectAndUiMirroringAreStable() {
  InputState state;
  state.gamepadAssignments[42] = 2;
  PlatformInput input(state);
  input.beginFrame();
  input.connectGamepad(42, "test pad");
  input.gamepadButton(42, "dpad_down", true);
  input.gamepadAxis(42, "left_x", 4.0F);
  assert(state.gamepads.size() == 1);
  assert(state.gamepads.front().player == 2);
  assert(state.gamepads.front().name == "test pad");
  assert(state.keysPressed.contains("ui_next"));
  assert(state.gamepads.front().axes.at("left_x") == 1.0F);

  input.beginFrame();
  input.gamepadButton(42, "dpad_down", false);
  assert(state.keysReleased.contains("ui_next"));
  input.disconnectGamepad(42);
  assert(state.gamepads.empty());
}

void textInputPreservesUtf8() {
  InputState state;
  PlatformInput input(state);
  input.beginFrame();
  input.text("Gr");
  input.text("\xC3\xBC\xC3\x9F");
  assert(state.textEntered == "Gr\xC3\xBC\xC3\x9F");
  input.beginFrame();
  assert(state.textEntered.empty());
}

} // namespace

int main() {
  keyboardTransitionsIgnoreRepeats();
  pointerDeltaAccumulatesWithinFrame();
  touchTerminalStateSurvivesOneFrame();
  gamepadDisconnectAndUiMirroringAreStable();
  textInputPreservesUtf8();
  return 0;
}
