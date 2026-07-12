#include "demi/runtime/input/InputActionResolver.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace demi::runtime::input {
namespace {

std::string normalized(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool bindingDown(const InputState &state, const std::string &input) {
  constexpr std::string_view mousePrefix = "mouse:";
  if (input.starts_with(mousePrefix))
    return state.mouseButtonsDown.contains(input.substr(mousePrefix.size()));
  return state.keysDown.contains(input);
}

bool bindingPressed(const InputState &state, const std::string &input) {
  // Mouse press edges are not represented separately yet. Keyboard and
  // controller bindings use the deterministic pressed snapshot.
  if (input.starts_with("mouse:"))
    return false;
  return state.keysPressed.contains(input);
}

const InputAction *findAction(const InputActionMap &actions,
                              const std::string_view name) {
  const auto found = actions.find(normalized(std::string(name)));
  return found == actions.end() ? nullptr : &found->second;
}

} // namespace

bool InputActionResolver::down(const InputActionMap &actions,
                               const InputState &state,
                               const std::string_view action) const {
  const InputAction *definition = findAction(actions, action);
  return definition != nullptr &&
         std::ranges::any_of(definition->bindings, [&](const auto &binding) {
           return bindingDown(state, binding.input);
         });
}

bool InputActionResolver::pressed(const InputActionMap &actions,
                                  const InputState &state,
                                  const std::string_view action) const {
  const InputAction *definition = findAction(actions, action);
  return definition != nullptr &&
         std::ranges::any_of(definition->bindings, [&](const auto &binding) {
           return bindingPressed(state, binding.input);
         });
}

float InputActionResolver::value(const InputActionMap &actions,
                                 const InputState &state,
                                 const std::string_view action) const {
  const InputAction *definition = findAction(actions, action);
  if (definition == nullptr)
    return 0.0F;
  float result = 0.0F;
  for (const InputBinding &binding : definition->bindings)
    if (bindingDown(state, binding.input))
      result += binding.scale;
  return std::clamp(result, -1.0F, 1.0F);
}

} // namespace demi::runtime::input
