#include "demi/runtime/ui/UiActionController.h"

#include "demi/runtime/ui/UiStateController.h"

namespace demi::runtime::ui {

bool UiActionController::apply(UiDocument &document,
                               const std::string_view action) const {
  std::vector<const UiActionEffect *> effects;
  if (const auto effect = document.actionEffects.find(std::string(action));
      effect != document.actionEffects.end())
    effects.push_back(&effect->second);
  for (const auto &node : document.nodes)
    if (const auto effect = node.actionEffects.find(std::string(action));
        effect != node.actionEffects.end())
      effects.push_back(&effect->second);
  if (effects.empty())
    return false;
  UiStateController state;
  for (const auto *effect : effects)
    for (const auto &id : effect->hide)
      (void)state.setVisible(document, id, false);
  for (const auto *effect : effects) {
    for (const auto &id : effect->show)
      (void)state.setVisible(document, id, true);
    if (!effect->focus.empty()) {
      const auto *node = state.find(document, effect->focus);
      if (node && node->visible && !node->disabled && node->focusable)
        document.focusedId = node->id;
    }
  }
  return true;
}

} // namespace demi::runtime::ui
