#include "demi/runtime/input/replay/InputReplay.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::runtime::input {

namespace {

void readStrings(const nlohmann::json &frame, const char *field,
                 std::unordered_set<std::string> &destination) {
  if (!frame.contains(field) || !frame[field].is_array())
    return;
  for (const auto &value : frame[field])
    if (value.is_string())
      destination.insert(value.get<std::string>());
}

Vec2 readVec2(const nlohmann::json &frame, const char *field) {
  if (!frame.contains(field) || !frame[field].is_array() ||
      frame[field].size() != 2)
    return {};
  return {.x = frame[field][0].get<float>(), .y = frame[field][1].get<float>()};
}

TouchPhase touchPhase(const std::string &phase) {
  if (phase == "began")
    return TouchPhase::Began;
  if (phase == "moved")
    return TouchPhase::Moved;
  if (phase == "ended")
    return TouchPhase::Ended;
  if (phase == "cancelled")
    return TouchPhase::Cancelled;
  return TouchPhase::Stationary;
}

void readGamepads(const nlohmann::json &frame, InputState &state) {
  if (!frame.contains("gamepads") || !frame["gamepads"].is_array())
    return;
  for (const auto &value : frame["gamepads"]) {
    if (!value.is_object())
      continue;
    GamepadState gamepad;
    gamepad.deviceId = value.value("device_id", -1);
    gamepad.player = value.value("player", -1);
    gamepad.name = value.value("name", std::string{});
    readStrings(value, "buttons_down", gamepad.buttonsDown);
    readStrings(value, "buttons_pressed", gamepad.buttonsPressed);
    readStrings(value, "buttons_released", gamepad.buttonsReleased);
    if (value.contains("axes") && value["axes"].is_object())
      for (const auto &[name, axis] : value["axes"].items())
        if (axis.is_number())
          gamepad.axes[name] = axis.get<float>();
    state.gamepads.push_back(std::move(gamepad));
  }
}

void readTouches(const nlohmann::json &frame, InputState &state) {
  if (!frame.contains("touches") || !frame["touches"].is_array())
    return;
  for (const auto &value : frame["touches"]) {
    if (!value.is_object())
      continue;
    state.touches.push_back(
        {.id = value.value("id", std::int64_t{0}),
         .phase = touchPhase(value.value("phase", std::string{"stationary"})),
         .position = readVec2(value, "position"),
         .delta = readVec2(value, "delta"),
         .pressure = value.value("pressure", 1.0F)});
  }
}

void readVec2Map(const nlohmann::json &frame, const char *field,
                 std::unordered_map<std::string, Vec2> &destination) {
  if (!frame.contains(field) || !frame[field].is_object())
    return;
  for (const auto &[name, value] : frame[field].items())
    destination[name] = readVec2(nlohmann::json{{"value", value}}, "value");
}

void readActions(const nlohmann::json &frame, InputState &state) {
  if (!frame.contains("actions") || !frame["actions"].is_object())
    return;
  for (const auto &[name, value] : frame["actions"].items()) {
    if (!value.is_object())
      continue;
    state.recordedActions[name] = {
        .held = value.value("held", false),
        .pressed = value.value("pressed", false),
        .released = value.value("released", false),
        .value = value.value("value", 0.0F),
        .vector = readVec2(value, "vector"),
        .source = value.value("source", std::string{}),
        .player = value.value("player", -1),
    };
  }
}

} // namespace

bool InputReplay::apply(const std::size_t frame, InputState &state) const {
  if (frame >= frames.size())
    return false;
  state = frames[frame];
  return true;
}

std::optional<InputReplay> loadInputReplay(const std::filesystem::path &path,
                                           std::string &error) {
  std::ifstream input(path);
  if (!input) {
    error = "could not open input replay: " + path.string();
    return std::nullopt;
  }
  try {
    const nlohmann::json document = nlohmann::json::parse(input);
    const int formatVersion = document.value("format_version", 0);
    if ((formatVersion != 1 && formatVersion != 2) ||
        !document.contains("frames") || !document["frames"].is_array()) {
      error = "input replay requires format_version 1 or 2 and a frames array";
      return std::nullopt;
    }
    InputReplay replay;
    replay.fixedTimestep = std::clamp(
        document.value("fixed_timestep", 1.0F / 60.0F), 0.001F, 1.0F);
    for (const nlohmann::json &frame : document["frames"]) {
      if (!frame.is_object()) {
        error = "every input replay frame must be an object";
        return std::nullopt;
      }
      InputState state;
      readStrings(frame, "keys_down", state.keysDown);
      readStrings(frame, "keys_pressed", state.keysPressed);
      readStrings(frame, "keys_released", state.keysReleased);
      readStrings(frame, "mouse_buttons_down", state.mouseButtonsDown);
      readStrings(frame, "mouse_buttons_pressed", state.mouseButtonsPressed);
      readStrings(frame, "mouse_buttons_released", state.mouseButtonsReleased);
      state.mousePosition = readVec2(frame, "mouse_position");
      state.mouseDelta = readVec2(frame, "mouse_delta");
      state.textEntered = frame.value("text_entered", std::string{});
      readGamepads(frame, state);
      readTouches(frame, state);
      readStrings(frame, "virtual_buttons_down", state.virtualButtonsDown);
      readStrings(frame, "virtual_buttons_pressed",
                  state.virtualButtonsPressed);
      readStrings(frame, "virtual_buttons_released",
                  state.virtualButtonsReleased);
      readVec2Map(frame, "virtual_axes", state.virtualAxes);
      readActions(frame, state);
      replay.frames.push_back(std::move(state));
    }
    return replay;
  } catch (const std::exception &exception) {
    error = "invalid input replay: " + std::string(exception.what());
    return std::nullopt;
  }
}

} // namespace demi::runtime::input
