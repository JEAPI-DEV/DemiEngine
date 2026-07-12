#include "demi/runtime/ui/UiActionController.h"

#include "demi/runtime/ui/UiStateController.h"

namespace demi::runtime::ui {

bool UiActionController::apply(UiDocument &document,
                               const std::string_view action) const {
  const auto effect = document.actionEffects.find(std::string(action));
  if (effect == document.actionEffects.end())
    return false;

  UiStateController state;
  for (const std::string &id : effect->second.hide)
    (void)state.setVisible(document, id, false);
  for (const std::string &id : effect->second.show)
    (void)state.setVisible(document, id, true);

  if (!effect->second.focus.empty()) {
    const UiNode *node = state.find(document, effect->second.focus);
    if (node != nullptr && node->visible && !node->disabled && node->focusable)
      document.focusedId = node->id;
  }
  return true;
}

} // namespace demi::runtime::ui
