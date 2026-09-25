#include "demi/runtime/input/GameplayInputService.h"

#include "demi/runtime/input/InputRebinding.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace demi::runtime::input {
namespace {

std::string normalized(std::string name) {
  std::ranges::transform(name, name.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return name;
}

} // namespace

void GameplayInputService::initialize(InputState &state) {
  state_ = &state;
  contexts_ = {"gameplay"};
}

void GameplayInputService::configure(
    const InputActionMap &actions,
    const std::filesystem::path &userDataDirectory) {
  actions_ = actions;
  userDataDirectory_ = userDataDirectory;
}

bool GameplayInputService::keyDown(const std::string &key) const {
  return state_ != nullptr && state_->keysDown.contains(normalized(key));
}

bool GameplayInputService::keyPressed(const std::string &key) const {
  return state_ != nullptr && state_->keysPressed.contains(normalized(key));
}

bool GameplayInputService::keyReleased(const std::string &key) const {
  return state_ != nullptr && state_->keysReleased.contains(normalized(key));
}

InputActionState GameplayInputService::resolve(const std::string &action,
                                               const int player) const {
  if (state_ == nullptr) {
    return {};
  }
  return InputActionResolver{}.resolve(actions_, *state_, action, player,
                                       &contexts_);
}

bool GameplayInputService::down(const std::string &action,
                                const int player) const {
  return resolve(action, player).held;
}

bool GameplayInputService::pressed(const std::string &action,
                                   const int player) const {
  return resolve(action, player).pressed;
}

bool GameplayInputService::released(const std::string &action,
                                    const int player) const {
  return resolve(action, player).released;
}

float GameplayInputService::value(const std::string &action,
                                  const int player) const {
  return resolve(action, player).value;
}

Vec2 GameplayInputService::rawVector(const std::string &action,
                                     const int player) const {
  return resolve(action, player).vector;
}

Vec2 GameplayInputService::vector(const std::string &action,
                                  const int player) const {
  Vec2 result = rawVector(action, player);
  const float length = std::sqrt(result.x * result.x + result.y * result.y);
  if (length > 1.0F) {
    result.x /= length;
    result.y /= length;
  }
  return result;
}

std::string GameplayInputService::source(const std::string &action,
                                         const int player) const {
  return resolve(action, player).source;
}

void GameplayInputService::enableContext(const std::string &context) {
  contexts_.insert(normalized(context));
}

void GameplayInputService::disableContext(const std::string &context) {
  contexts_.erase(normalized(context));
}

bool GameplayInputService::contextEnabled(const std::string &context) const {
  return contexts_.contains(normalized(context));
}

bool GameplayInputService::rebind(const std::string &action,
                                  const std::size_t bindingIndex,
                                  const std::string &control, const int player,
                                  std::string &error) {
  return InputRebinding::rebind(actions_, action, bindingIndex,
                                {.input = control, .player = player}, error);
}

std::filesystem::path
GameplayInputService::bindingPath(const std::filesystem::path &path) const {
  return path.is_absolute() ? path : userDataDirectory_ / path;
}

bool GameplayInputService::saveBindings(const std::filesystem::path &path,
                                        std::string &error) const {
  if (path.is_relative() && userDataDirectory_.empty()) {
    error = "cannot save relative input bindings before user-data storage is "
            "configured";
    return false;
  }
  return InputRebinding::save(actions_, bindingPath(path), error);
}

bool GameplayInputService::loadBindings(const std::filesystem::path &path,
                                        std::string &error) {
  if (path.is_relative() && userDataDirectory_.empty()) {
    error = "cannot load relative input bindings before user-data storage is "
            "configured";
    return false;
  }
  return InputRebinding::load(actions_, bindingPath(path), error);
}

bool GameplayInputService::assignGamepad(const int deviceId, const int player) {
  if (state_ == nullptr || player < 0) {
    return false;
  }
  const auto found =
      std::ranges::find(state_->gamepads, deviceId, &GamepadState::deviceId);
  if (found == state_->gamepads.end()) {
    return false;
  }
  found->player = player;
  state_->gamepadAssignments[deviceId] = player;
  return true;
}

} // namespace demi::runtime::input
