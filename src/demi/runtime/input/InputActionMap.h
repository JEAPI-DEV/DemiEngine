#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::input {

struct InputBinding {
  std::string input;
  float scale = 1.0F;
};

struct InputAction {
  std::vector<InputBinding> bindings;
};

using InputActionMap = std::unordered_map<std::string, InputAction>;

} // namespace demi::runtime::input
