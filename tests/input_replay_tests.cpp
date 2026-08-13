#include "demi/runtime/input/replay/InputReplay.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  const auto path =
      std::filesystem::temp_directory_path() / "demi_input_fixture.replay.json";
  {
    std::ofstream output(path);
    output << R"({
      "format_version": 2,
      "fixed_timestep": 0.02,
      "frames": [
        {"keys_down": ["d"], "keys_pressed": ["space"],
         "keys_released": ["a"], "mouse_position": [10, 20],
         "mouse_scroll": [1, -2],
         "gamepads": [{
           "device_id": 2, "player": 1, "name": "fixture",
           "buttons_down": ["south"], "buttons_pressed": ["south"],
           "axes": {"left_x": 0.75, "left_y": -0.25}
         }],
         "touches": [{
           "id": 42, "phase": "moved", "position": [30, 40],
           "delta": [2, 3], "pressure": 0.5
         }],
         "virtual_axes": {"move": [0.25, -0.5]},
         "actions": {
           "jump": {
             "held": true, "pressed": true, "value": 1,
             "vector": [1, 0], "source": "gamepad:south", "player": 1
           }
         }},
        {"keys_down": ["a"], "text_entered": "x",
         "text_composition": "candidate",
         "text_composition_selection_start": 2,
         "text_composition_selection_length": 3}
      ]
    })";
  }
  std::string error;
  const auto replay = demi::runtime::input::loadInputReplay(path, error);
  demi::runtime::InputState state;
  if (!replay || replay->frames.size() != 2 || !replay->apply(0, state) ||
      !state.keysDown.contains("d") || !state.keysPressed.contains("space") ||
      !state.keysReleased.contains("a") || state.gamepads.size() != 1 ||
      state.gamepads[0].player != 1 ||
      state.gamepads[0].axes["left_x"] != 0.75F || state.touches.size() != 1 ||
      state.touches[0].id != 42 || state.virtualAxes["move"].y != -0.5F ||
      !state.recordedActions["jump"].pressed ||
      state.mousePosition.y != 20.0F || state.mouseScroll.y != -2.0F ||
      !replay->apply(1, state) || !state.keysDown.contains("a") ||
      state.keysDown.contains("d") || state.textEntered != "x" ||
      state.textComposition != "candidate" ||
      state.textCompositionSelectionStart != 2 ||
      state.textCompositionSelectionLength != 3 ||
      !state.textCompositionChanged || replay->apply(2, state)) {
    std::cerr << "Input replay parsing or frame application failed: " << error
              << '\n';
    return 1;
  }
  replay->applyOrNeutral(2, state);
  if (!state.keysDown.empty() || !state.keysPressed.empty() ||
      !state.recordedActions.empty()) {
    std::cerr << "Replay neutral-frame application failed.\n";
    return 1;
  }
  std::filesystem::remove(path);
  return 0;
}
