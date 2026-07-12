#include "demi/runtime/ui/UiStateController.h"

#include <algorithm>
#include <ranges>

namespace demi::runtime::ui {

UiNode *UiStateController::find(UiDocument &document,
                                const std::string_view id) const {
  const auto found = std::ranges::find(document.nodes, id, &UiNode::id);
  return found == document.nodes.end() ? nullptr : &*found;
}

const UiNode *UiStateController::find(const UiDocument &document,
                                      const std::string_view id) const {
  const auto found = std::ranges::find(document.nodes, id, &UiNode::id);
  return found == document.nodes.end() ? nullptr : &*found;
}

bool UiStateController::setText(UiDocument &document, const std::string_view id,
                                std::string text) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->text = std::move(text);
  return true;
}

bool UiStateController::setValue(UiDocument &document,
                                 const std::string_view id,
                                 const float value) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->value = std::clamp(value, node->minimum, node->maximum);
  return true;
}

bool UiStateController::setChecked(UiDocument &document,
                                   const std::string_view id,
                                   const bool checked) const {
  UiNode *node = find(document, id);
  if (node == nullptr || node->type != "toggle")
    return false;
  node->checked = checked;
  return true;
}

bool UiStateController::setDisabled(UiDocument &document,
                                    const std::string_view id,
                                    const bool disabled) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->disabled = disabled;
  if (disabled && document.focusedId == id)
    document.focusedId.clear();
  return true;
}

bool UiStateController::setVisible(UiDocument &document,
                                   const std::string_view id,
                                   const bool visible) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->visible = visible;
  return true;
}

} // namespace demi::runtime::ui
