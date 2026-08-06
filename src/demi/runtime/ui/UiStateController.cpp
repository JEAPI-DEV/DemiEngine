#include "demi/runtime/ui/UiStateController.h"

#include <algorithm>
#include <ranges>

namespace demi::runtime::ui {
namespace {
bool belongsTo(const UiDocument &document, std::string id,
               const std::string_view ancestor) {
  while (!id.empty()) {
    if (id == ancestor) return true;
    const auto node = std::ranges::find(document.nodes, id, &UiNode::id);
    if (node == document.nodes.end()) return false;
    id = node->parent;
  }
  return false;
}
void cancelSubtreeInteraction(UiDocument &document,
                              const std::string_view root) {
  if (belongsTo(document, document.focusedId, root)) document.focusedId.clear();
  if (belongsTo(document, document.pointerCaptureId, root))
    document.pointerCaptureId.clear();
  std::erase_if(document.pointerCaptures, [&](const auto &capture) {
    return belongsTo(document, capture.second, root);
  });
}
} // namespace

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
  if (disabled) cancelSubtreeInteraction(document, id);
  return true;
}

bool UiStateController::setVisible(UiDocument &document,
                                   const std::string_view id,
                                   const bool visible) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->visible = visible;
  if (!visible) cancelSubtreeInteraction(document, id);
  return true;
}

} // namespace demi::runtime::ui
