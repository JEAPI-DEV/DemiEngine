#include "demi/runtime/input/InputActionParser.h"
#include "demi/runtime/input/InputActionResolver.h"
#include "demi/runtime/input/InputRebinding.h"
#include "demi/runtime/input/TouchGestureRecognizer.h"
#include "demi/runtime/platform/ApplicationServices.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_set>

int main() {
  const auto actions =
      demi::runtime::input::parseInputActions(nlohmann::json::parse(R"({
        "input": {"actions": {
          "move": {"type":"axis1d","context":"gameplay","bindings":[
            {"input": "key:A", "scale": -1},
            {"input": "key:d", "scale": 1}
          ]},
          "jump": {"type":"button","context":"gameplay","bindings": [
            {"input":"key:space"},{"input":"key:up"}]},
          "fire": {"type":"button","context":"gameplay","bindings":[
            {"input":"mouse:left"}]},
          "move2": {
            "type": "vector2",
            "context": "gameplay",
            "bindings": [
              {"input": "key:a", "vector": [-1, 0]},
              {"input": "gamepad:stick:left", "deadzone": 0.2}
            ]
          },
          "menu_accept": {
            "type": "button",
            "context": "menu",
            "bindings": [{"input": "gamepad:south", "player": 1}]
          },
          "touch_x": {
            "type": "axis1d",
            "context": "gameplay",
            "bindings": [{"input":"virtual:move:x"}]
          },
          "touch_y": {
            "type": "axis1d",
            "context": "gameplay",
            "bindings": [{"input": "virtual:move:y", "invert": true}]
          }
        }}
      })"));

  const auto legacyActions =
      demi::runtime::input::parseInputActions(nlohmann::json::parse(R"({
        "input":{"actions":{
          "array_form":["space"],
          "string_binding":{"type":"button","context":"gameplay","bindings":["space"]},
          "key_alias":{"type":"button","context":"gameplay","bindings":[{"key":"space"}]},
          "unqualified":{"type":"button","context":"gameplay","bindings":[{"input":"space"}]},
          "type_alias":{"type":"axis","context":"gameplay","bindings":[{"input":"key:a"}]}
        }}
      })"));
  if (!legacyActions.empty()) {
    std::cerr << "Legacy input forms were still accepted.\n";
    return 1;
  }

  demi::runtime::InputState state;
  state.keysDown = {"a", "space"};
  state.keysPressed = {"space"};
  state.mouseButtonsDown = {"left"};
  state.mouseButtonsPressed = {"left"};
  state.gamepads.push_back({
      .deviceId = 4,
      .player = 1,
      .buttonsDown = {"south"},
      .buttonsPressed = {"south"},
      .axes = {{"left_x", 0.75F}, {"left_y", -0.5F}},
  });
  state.virtualAxes["move"] = {.x = 0.4F, .y = 0.6F};
  const demi::runtime::input::InputActionResolver resolver;
  if (actions.size() != 7 || resolver.value(actions, state, "MOVE") != -1.0F ||
      !resolver.down(actions, state, "jump") ||
      !resolver.pressed(actions, state, "jump") ||
      !resolver.down(actions, state, "fire") ||
      !resolver.pressed(actions, state, "fire")) {
    std::cerr << "Input action parsing or resolution failed: actions="
              << actions.size()
              << " move=" << resolver.value(actions, state, "MOVE")
              << " jump_down=" << resolver.down(actions, state, "jump")
              << " jump_pressed=" << resolver.pressed(actions, state, "jump")
              << " fire_down=" << resolver.down(actions, state, "fire")
              << " fire_pressed=" << resolver.pressed(actions, state, "fire")
              << '\n';
    return 1;
  }

  const demi::runtime::Vec2 gamepadMove =
      resolver.vector(actions, state, "move2", 1);
  const std::unordered_set<std::string> gameplay{"gameplay"};
  const std::unordered_set<std::string> menu{"menu"};
  if (gamepadMove.x != -0.25F || gamepadMove.y != -0.5F ||
      resolver.down(actions, state, "menu_accept", 1, &gameplay) ||
      !resolver.pressed(actions, state, "menu_accept", 1, &menu) ||
      resolver.value(actions, state, "touch_x") != 0.4F ||
      resolver.value(actions, state, "touch_y") != -0.6F ||
      resolver.resolve(actions, state, "menu_accept", 1, &menu).source !=
          "gamepad:4:south") {
    std::cerr << "Vector, player, or context action resolution failed.\n";
    return 1;
  }

  state.keysDown.insert("d");
  if (resolver.value(actions, state, "move") != 0.0F ||
      resolver.down(actions, state, "missing")) {
    std::cerr
        << "Input action cancellation or missing-action behavior failed.\n";
    return 1;
  }

  auto rebound = actions;
  std::string error;
  if (!demi::runtime::input::InputRebinding::rebind(
          rebound, "jump", 0, {.input = "key:j"}, error)) {
    std::cerr << "Runtime rebind failed: " << error << '\n';
    return 1;
  }
  state.keysDown.insert("j");
  const auto bindingPath =
      std::filesystem::temp_directory_path() / "demi_bindings.json";
  auto restored = actions;
  if (!demi::runtime::input::InputRebinding::save(rebound, bindingPath,
                                                   error) ||
      !demi::runtime::input::InputRebinding::load(restored, bindingPath,
                                                   error) ||
      !resolver.down(restored, state, "jump")) {
    std::cerr << "Runtime binding persistence failed: " << error << '\n';
    return 1;
  }
  std::filesystem::remove(bindingPath);

  demi::runtime::input::TouchGestureRecognizer gestures;
  (void)gestures.update(
      {{.id = 7,
        .phase = demi::runtime::TouchPhase::Began,
        .position = {10.0F, 10.0F}}},
      0.01F);
  const auto tap = gestures.update(
      {{.id = 7,
        .phase = demi::runtime::TouchPhase::Ended,
        .position = {10.0F, 10.0F}}},
      0.05F);
  if (tap.size() != 1 ||
      tap.front().type != demi::runtime::input::GestureType::Tap) {
    std::cerr << "Deterministic touch gesture recognition failed.\n";
    return 1;
  }

  demi::runtime::platform::ApplicationServices application;
  application.updateDisplay(
      2400, 1080, 192.0F,
      {.left = 40.0F, .top = 20.0F, .right = 30.0F, .bottom = 10.0F});
  std::string clipboard;
  application.setClipboardHandlers([&] { return clipboard; },
                                   [&](const std::string &value) {
                                     clipboard = value;
                                   });
  application.setClipboard("portable");
  application.requestOrientation(
      demi::runtime::platform::Orientation::Landscape);
  if (application.uiScale() != 2.0F ||
      application.safeArea().left != 40.0F ||
      application.orientation() !=
          demi::runtime::platform::Orientation::Landscape ||
      application.requestedOrientation() !=
          demi::runtime::platform::Orientation::Landscape ||
      application.clipboard() != "portable") {
    std::cerr << "Cross-platform application service state failed.\n";
    return 1;
  }
  return 0;
}
