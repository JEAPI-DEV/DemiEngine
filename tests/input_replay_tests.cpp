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
      "format_version": 1,
      "fixed_timestep": 0.02,
      "frames": [
        {"keys_down": ["d"], "keys_pressed": ["space"],
         "mouse_position": [10, 20]},
        {"keys_down": ["a"], "text_entered": "x"}
      ]
    })";
  }
  std::string error;
  const auto replay = demi::runtime::input::loadInputReplay(path, error);
  demi::runtime::InputState state;
  if (!replay || replay->frames.size() != 2 || !replay->apply(0, state) ||
      !state.keysDown.contains("d") || !state.keysPressed.contains("space") ||
      state.mousePosition.y != 20.0F || !replay->apply(1, state) ||
      !state.keysDown.contains("a") || state.keysDown.contains("d") ||
      state.textEntered != "x" || replay->apply(2, state)) {
    std::cerr << "Input replay parsing or frame application failed: " << error
              << '\n';
    return 1;
  }
  std::filesystem::remove(path);
  return 0;
}
