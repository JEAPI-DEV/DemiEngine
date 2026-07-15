#pragma once

#include <string>
#include <unordered_set>

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

struct InputState {
  std::unordered_set<std::string> keysDown;
  std::unordered_set<std::string> keysPressed;
  std::unordered_set<std::string> mouseButtonsDown;
  Vec2 mousePosition;
  Vec2 mouseDelta;
  std::string textEntered;
};

struct DebugLine {
  Vec2 start;
  Vec2 end;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  float width = 1.0F;
};

} // namespace demi::runtime
