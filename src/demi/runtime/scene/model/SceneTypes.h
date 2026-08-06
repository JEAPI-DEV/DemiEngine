#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};
struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};
struct Color {
  float r = 0.08F;
  float g = 0.09F;
  float b = 0.12F;
  float a = 1.0F;
};

enum class TouchPhase { Began, Moved, Stationary, Ended, Cancelled };

struct TouchPoint {
  std::int64_t id = 0;
  TouchPhase phase = TouchPhase::Stationary;
  Vec2 position;
  Vec2 delta;
  float pressure = 1.0F;
};

struct GamepadState {
  int deviceId = -1;
  int player = -1;
  std::string name;
  std::unordered_set<std::string> buttonsDown;
  std::unordered_set<std::string> buttonsPressed;
  std::unordered_set<std::string> buttonsReleased;
  std::unordered_map<std::string, float> axes;
};

struct RecordedActionState {
  bool held = false;
  bool pressed = false;
  bool released = false;
  float value = 0.0F;
  Vec2 vector;
  std::string source;
  int player = -1;
};

struct InputState {
  std::unordered_set<std::string> keysDown;
  std::unordered_set<std::string> keysPressed;
  std::unordered_set<std::string> keysReleased;
  std::unordered_set<std::string> mouseButtonsDown;
  std::unordered_set<std::string> mouseButtonsPressed;
  std::unordered_set<std::string> mouseButtonsReleased;
  Vec2 mousePosition;
  Vec2 mouseDelta;
  std::string textEntered;
  std::string textComposition;
  std::size_t textCompositionSelectionStart = 0;
  std::size_t textCompositionSelectionLength = 0;
  bool textCompositionChanged = false;
  std::vector<GamepadState> gamepads;
  std::unordered_map<int, int> gamepadAssignments;
  std::vector<TouchPoint> touches;
  std::unordered_set<std::string> virtualButtonsDown;
  std::unordered_set<std::string> virtualButtonsPressed;
  std::unordered_set<std::string> virtualButtonsReleased;
  std::unordered_map<std::string, Vec2> virtualAxes;
  std::unordered_map<std::string, RecordedActionState> recordedActions;
};

struct DebugLine {
  Vec2 start;
  Vec2 end;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  float width = 1.0F;
};

} // namespace demi::runtime
