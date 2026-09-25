#pragma once

#include "demi/runtime/input/InputActionResolver.h"

#include <filesystem>

namespace demi::runtime::input {

// Owns gameplay bindings and contexts over the runtime's live input snapshot.
// The snapshot must outlive the service's use of it.
class GameplayInputService {
public:
  void initialize(InputState &state);
  void configure(const InputActionMap &actions,
                 const std::filesystem::path &userDataDirectory);

  [[nodiscard]] bool keyDown(const std::string &key) const;
  [[nodiscard]] bool keyPressed(const std::string &key) const;
  [[nodiscard]] bool keyReleased(const std::string &key) const;
  [[nodiscard]] bool down(const std::string &action, int player = -1) const;
  [[nodiscard]] bool pressed(const std::string &action, int player = -1) const;
  [[nodiscard]] bool released(const std::string &action, int player = -1) const;
  [[nodiscard]] float value(const std::string &action, int player = -1) const;
  [[nodiscard]] Vec2 rawVector(const std::string &action,
                               int player = -1) const;
  [[nodiscard]] Vec2 vector(const std::string &action, int player = -1) const;
  [[nodiscard]] std::string source(const std::string &action,
                                   int player = -1) const;

  void enableContext(const std::string &context);
  void disableContext(const std::string &context);
  [[nodiscard]] bool contextEnabled(const std::string &context) const;
  [[nodiscard]] bool rebind(const std::string &action, std::size_t bindingIndex,
                            const std::string &control, int player,
                            std::string &error);
  [[nodiscard]] bool saveBindings(const std::filesystem::path &path,
                                  std::string &error) const;
  [[nodiscard]] bool loadBindings(const std::filesystem::path &path,
                                  std::string &error);
  [[nodiscard]] bool assignGamepad(int deviceId, int player);

private:
  [[nodiscard]] InputActionState resolve(const std::string &action,
                                         int player) const;
  [[nodiscard]] std::filesystem::path
  bindingPath(const std::filesystem::path &path) const;

  InputState *state_ = nullptr;
  InputActionMap actions_;
  std::unordered_set<std::string> contexts_;
  std::filesystem::path userDataDirectory_;
};

} // namespace demi::runtime::input
