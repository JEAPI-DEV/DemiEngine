#pragma once

#include "demi/runtime/input/InputActionMap.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string_view>

namespace demi::runtime::input {

class InputActionResolver {
public:
  [[nodiscard]] bool down(const InputActionMap &actions,
                          const InputState &state,
                          std::string_view action) const;
  [[nodiscard]] bool pressed(const InputActionMap &actions,
                             const InputState &state,
                             std::string_view action) const;
  [[nodiscard]] float value(const InputActionMap &actions,
                            const InputState &state,
                            std::string_view action) const;
};

} // namespace demi::runtime::input
