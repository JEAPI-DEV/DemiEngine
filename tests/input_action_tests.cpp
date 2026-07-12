#include "demi/runtime/input/InputActionParser.h"
#include "demi/runtime/input/InputActionResolver.h"

#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  const auto actions =
      demi::runtime::input::parseInputActions(nlohmann::json::parse(R"({
        "input": {"actions": {
          "move": [
            {"input": "A", "scale": -1},
            {"input": "d", "scale": 1}
          ],
          "jump": {"bindings": ["space", "up"]},
          "fire": ["mouse:left"]
        }}
      })"));

  demi::runtime::InputState state;
  state.keysDown = {"a", "space"};
  state.keysPressed = {"space"};
  state.mouseButtonsDown = {"left"};
  const demi::runtime::input::InputActionResolver resolver;
  if (actions.size() != 3 || resolver.value(actions, state, "MOVE") != -1.0F ||
      !resolver.down(actions, state, "jump") ||
      !resolver.pressed(actions, state, "jump") ||
      !resolver.down(actions, state, "fire") ||
      resolver.pressed(actions, state, "fire")) {
    std::cerr << "Input action parsing or resolution failed.\n";
    return 1;
  }

  state.keysDown.insert("d");
  if (resolver.value(actions, state, "move") != 0.0F ||
      resolver.down(actions, state, "missing")) {
    std::cerr
        << "Input action cancellation or missing-action behavior failed.\n";
    return 1;
  }
  return 0;
}
