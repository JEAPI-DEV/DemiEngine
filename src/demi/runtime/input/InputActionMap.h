#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::input {

enum class InputActionType { Button, Axis1D, Vector2 };

struct InputBinding {
  std::string input;
  float scale = 1.0F;
  float x = 0.0F;
  float y = 0.0F;
  float deadzone = 0.0F;
  bool invert = false;
  bool normalize = false;
  int player = -1;
};

struct InputAction {
  InputActionType type = InputActionType::Button;
  std::string context = "gameplay";
  int player = -1;
  std::vector<InputBinding> bindings;
};

using InputActionMap = std::unordered_map<std::string, InputAction>;

} // namespace demi::runtime::input
