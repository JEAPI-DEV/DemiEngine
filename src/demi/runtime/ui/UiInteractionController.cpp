#include "demi/runtime/ui/UiInteractionController.h"
#include <algorithm>
namespace demi::runtime::ui {
namespace {
bool interactive(const UiNode &node) {
  return node.visible && !node.disabled && node.focusable;
}
bool interactive(const UiDocument &document, const UiNode &node) {
  if (!interactive(node))
    return false;
  std::string parent = node.parent;
  while (!parent.empty()) {
    const auto found = std::ranges::find(document.nodes, parent, &UiNode::id);
    if (found == document.nodes.end() || !found->visible || found->disabled)
      return false;
    parent = found->parent;
  }
  return true;
}
bool contains(const Rect &rect, const Vec2 point) {
  return point.x >= rect.x && point.y >= rect.y &&
         point.x <= rect.x + rect.width && point.y <= rect.y + rect.height;
}
bool belongsTo(const UiDocument &document, const UiNode &node,
               const std::string &ancestor) {
  if (ancestor.empty())
    return true;
  std::string current = node.id;
  while (!current.empty()) {
    if (current == ancestor)
      return true;
    const auto found = std::ranges::find(document.nodes, current, &UiNode::id);
    if (found == document.nodes.end())
      return false;
    current = found->parent;
  }
  return false;
}
std::string activeModal(const UiDocument &document) {
  for (auto node = document.nodes.rbegin(); node != document.nodes.rend();
       ++node)
    if (node->type == "modal" && node->visible && !node->disabled)
      return node->id;
  return {};
}
} // namespace
bool UiInteractionController::focusNext(UiDocument &document,
                                        const bool reverse) const {
  std::vector<std::size_t> candidates;
  const std::string modal = activeModal(document);
  for (std::size_t i = 0; i < document.nodes.size(); ++i)
    if (interactive(document, document.nodes[i]) &&
        belongsTo(document, document.nodes[i], modal))
      candidates.push_back(i);
  if (candidates.empty()) {
    document.focusedId.clear();
    return false;
  }
  auto current = std::ranges::find_if(candidates, [&](std::size_t i) {
    return document.nodes[i].id == document.focusedId;
  });
  std::size_t position =
      current == candidates.end()
          ? (reverse ? candidates.size() - 1 : 0)
          : static_cast<std::size_t>(current - candidates.begin());
  if (current != candidates.end())
    position = reverse ? (position + candidates.size() - 1) % candidates.size()
                       : (position + 1) % candidates.size();
  document.focusedId = document.nodes[candidates[position]].id;
  return true;
}
bool UiInteractionController::capturePointer(UiDocument &document,
                                             const Vec2 position) const {
  for (auto iterator = document.nodes.rbegin();
       iterator != document.nodes.rend(); ++iterator) {
    if (interactive(document, *iterator) &&
        contains(iterator->resolved, position)) {
      document.pointerCaptureId = iterator->id;
      document.focusedId = iterator->id;
      return true;
    }
  }
  return false;
}
bool UiInteractionController::updatePointer(UiDocument &document,
                                            const Vec2 position) const {
  const auto found =
      std::ranges::find(document.nodes, document.pointerCaptureId, &UiNode::id);
  if (found == document.nodes.end() || found->type != "slider" ||
      found->resolved.width <= 0.0F)
    return false;
  const float fraction = std::clamp(
      (position.x - found->resolved.x) / found->resolved.width, 0.0F, 1.0F);
  found->value = found->minimum + fraction * (found->maximum - found->minimum);
  return true;
}
void UiInteractionController::releasePointer(UiDocument &document) const {
  document.pointerCaptureId.clear();
}
std::optional<std::string>
UiInteractionController::activateFocused(UiDocument &document) const {
  const auto found =
      std::ranges::find_if(document.nodes, [&](const UiNode &node) {
        return node.id == document.focusedId && interactive(document, node);
      });
  if (found == document.nodes.end() || found->action.empty())
    return std::nullopt;
  if (found->type == "toggle")
    found->checked = !found->checked;
  return found->action;
}
} // namespace demi::runtime::ui
