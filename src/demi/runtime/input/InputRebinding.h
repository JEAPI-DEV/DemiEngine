#pragma once

#include "demi/runtime/input/InputActionMap.h"

#include <filesystem>
#include <string>

namespace demi::runtime::input {

class InputRebinding {
public:
  [[nodiscard]] static bool rebind(InputActionMap &actions,
                                   const std::string &action,
                                   std::size_t bindingIndex,
                                   InputBinding binding,
                                   std::string &error);
  [[nodiscard]] static bool save(const InputActionMap &actions,
                                 const std::filesystem::path &path,
                                 std::string &error);
  [[nodiscard]] static bool load(InputActionMap &actions,
                                 const std::filesystem::path &path,
                                 std::string &error);
};

} // namespace demi::runtime::input
