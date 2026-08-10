#include "demi/runtime/platform/PlatformInput.h"

#include <cassert>
#include <cmath>

using demi::runtime::InputState;
using demi::runtime::TouchPhase;
using demi::runtime::Vec2;
using demi::runtime::platform::PlatformInput;
using demi::runtime::platform::PointerMotion;
using demi::runtime::platform::pointerMotionInDrawablePixels;

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

void pointerScrollAccumulatesWithinFrame() {
  InputState state;
  PlatformInput input(state);
  input.beginFrame();
  input.pointerScroll(1.0F, -2.0F);
  input.pointerScroll(0.5F, 3.0F);
  assert(state.mouseScroll.x == 1.5F);
  assert(state.mouseScroll.y == 1.0F);
  input.beginFrame();
  assert(state.mouseScroll.x == 0.0F);
  assert(state.mouseScroll.y == 0.0F);
}

void windowPointerCoordinatesMatchDrawablePixels() {
  const PointerMotion scaled = pointerMotionInDrawablePixels(
      {.position = {160.0F, 90.0F}, .delta = {4.0F, -3.0F}}, {960.0F, 540.0F},
      {1920.0F, 1080.0F});
  assert(scaled.position.x == 320.0F);
  assert(scaled.position.y == 180.0F);
  assert(scaled.delta.x == 8.0F);
  assert(scaled.delta.y == -6.0F);

  const PointerMotion nonUniform = pointerMotionInDrawablePixels(
      {.position = {200.0F, 150.0F}, .delta = {10.0F, 10.0F}},
      {1000.0F, 600.0F}, {1500.0F, 1200.0F});
  assert(nonUniform.position.x == 300.0F);
  assert(nonUniform.position.y == 300.0F);
  assert(nonUniform.delta.x == 15.0F);
  assert(nonUniform.delta.y == 20.0F);

  const PointerMotion safeFallback = pointerMotionInDrawablePixels(
      {.position = {2.0F, 3.0F}, .delta = {}}, {0.0F, -1.0F}, {0.0F, -1.0F});
  assert(safeFallback.position.x == 2.0F);
  assert(safeFallback.position.y == 3.0F);
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
  input.touch(7, TouchPhase::Ended, Vec2{11.0F, 12.0F}, Vec2{1.0F, 0.0F}, 0.0F);
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

void compositionPersistsUntilChangedOrCommitted() {
  InputState state;
  PlatformInput input(state);
  input.beginFrame();
  input.composition("\xE3\x81\xAB", 1, 3);
  assert(state.textComposition == "\xE3\x81\xAB");
  assert(state.textCompositionSelectionStart == 1);
  assert(state.textCompositionSelectionLength == 3);
  assert(state.textCompositionChanged);

  input.beginFrame();
  assert(state.textComposition == "\xE3\x81\xAB");
  assert(!state.textCompositionChanged);
  input.composition("candidate", -4, -2);
  assert(state.textCompositionSelectionStart == 0);
  assert(state.textCompositionSelectionLength == 0);

  input.text("accepted");
  assert(state.textEntered == "accepted");
  assert(state.textComposition.empty());
  assert(state.textCompositionChanged);
}

} // namespace

int main() {
  keyboardTransitionsIgnoreRepeats();
  pointerDeltaAccumulatesWithinFrame();
  pointerScrollAccumulatesWithinFrame();
  windowPointerCoordinatesMatchDrawablePixels();
  touchTerminalStateSurvivesOneFrame();
  gamepadDisconnectAndUiMirroringAreStable();
  textInputPreservesUtf8();
  compositionPersistsUntilChangedOrCommitted();
  return 0;
}
