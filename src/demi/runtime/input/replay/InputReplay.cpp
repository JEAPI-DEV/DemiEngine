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
    if (document.value("format_version", 0) != 1 ||
        !document.contains("frames") || !document["frames"].is_array()) {
      error = "input replay requires format_version 1 and a frames array";
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
      readStrings(frame, "mouse_buttons_down", state.mouseButtonsDown);
      state.mousePosition = readVec2(frame, "mouse_position");
      state.mouseDelta = readVec2(frame, "mouse_delta");
      state.textEntered = frame.value("text_entered", std::string{});
      replay.frames.push_back(std::move(state));
    }
    return replay;
  } catch (const std::exception &exception) {
    error = "invalid input replay: " + std::string(exception.what());
    return std::nullopt;
  }
}

} // namespace demi::runtime::input
