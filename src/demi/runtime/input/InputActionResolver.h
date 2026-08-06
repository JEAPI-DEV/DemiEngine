#pragma once

#include "demi/runtime/input/InputActionMap.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <string_view>
#include <unordered_set>

namespace demi::runtime::input {

struct InputActionState {
  bool held = false;
  bool pressed = false;
  bool released = false;
  float value = 0.0F;
  Vec2 vector;
  std::string source;
};

class InputActionResolver {
public:
  [[nodiscard]] InputActionState
  resolve(const InputActionMap &actions, const InputState &state,
          std::string_view action, int player = -1,
          const std::unordered_set<std::string> *contexts = nullptr) const;
  [[nodiscard]] bool down(const InputActionMap &actions,
                          const InputState &state,
                          std::string_view action, int player = -1,
                          const std::unordered_set<std::string> *contexts =
                              nullptr) const;
  [[nodiscard]] bool pressed(const InputActionMap &actions,
                             const InputState &state,
                             std::string_view action, int player = -1,
                             const std::unordered_set<std::string> *contexts =
                                 nullptr) const;
  [[nodiscard]] bool released(const InputActionMap &actions,
                              const InputState &state,
                              std::string_view action, int player = -1,
                              const std::unordered_set<std::string> *contexts =
                                  nullptr) const;
  [[nodiscard]] float value(const InputActionMap &actions,
                            const InputState &state,
                            std::string_view action, int player = -1,
                            const std::unordered_set<std::string> *contexts =
                                nullptr) const;
  [[nodiscard]] Vec2 vector(const InputActionMap &actions,
                            const InputState &state,
                            std::string_view action, int player = -1,
                            const std::unordered_set<std::string> *contexts =
                                nullptr) const;
};

} // namespace demi::runtime::input
