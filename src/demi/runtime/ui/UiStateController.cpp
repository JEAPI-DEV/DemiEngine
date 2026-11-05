#include "demi/runtime/ui/UiStateController.h"

#include "demi/runtime/ui/UiEventQueue.h"

#include <algorithm>
#include <ranges>

namespace demi::runtime::ui {
namespace {
bool belongsTo(const UiDocument &document, std::string id,
               const std::string_view ancestor) {
  while (!id.empty()) {
    if (id == ancestor)
      return true;
    const auto node = std::ranges::find(document.nodes, id, &UiNode::id);
    if (node == document.nodes.end())
      return false;
    id = node->parent;
  }
  return false;
}
void cancelSubtreeInteraction(UiDocument &document,
                              const std::string_view root) {
  if (belongsTo(document, document.focusedId, root)) {
    const auto focused =
        std::ranges::find(document.nodes, document.focusedId, &UiNode::id);
    if (focused != document.nodes.end())
      TextEditingEngine::clearComposition(focused->textEdit);
  }
  UiEventQueue::cancelSubtree(document, root, "state_change");
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

namespace {
void updateRect(UiDocument &document, UiNode &node, Vec2 position, Vec2 size) {
  const bool positionChanged = node.layout.position.x != position.x ||
                               node.layout.position.y != position.y;
  const bool sizeChanged =
      node.layout.size.x != size.x || node.layout.size.y != size.y;
  if (positionChanged || sizeChanged) {
    node.layout.position = position;
    node.layout.size = size;
    document.invalidateLayout();
  }
}
} // namespace

bool UiStateController::setRect(UiDocument &document, std::string_view id,
                                Vec2 position, Vec2 size) const {
  if (UiNode *node = find(document, id)) {
    updateRect(document, *node, position, size);
    return true;
  }
  return false;
}

bool UiStateController::setPosition(UiDocument &document, std::string_view id,
                                    Vec2 position) const {
  if (UiNode *node = find(document, id)) {
    updateRect(document, *node, position, node->layout.size);
    return true;
  }
  return false;
}

bool UiStateController::setSize(UiDocument &document, std::string_view id,
                                Vec2 size) const {
  if (UiNode *node = find(document, id)) {
    updateRect(document, *node, node->layout.position, size);
    return true;
  }
  return false;
}

bool UiStateController::setText(UiDocument &document, const std::string_view id,
                                std::string text) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  node->text = std::move(text);
  TextEditingEngine::clearComposition(node->textEdit);
  TextEditingEngine::normalize(node->text, node->textEdit);
  return true;
}

bool UiStateController::setValue(UiDocument &document,
                                 const std::string_view id,
                                 const float value) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  const float next = std::clamp(value, node->minimum, node->maximum);
  if (node->type == "scroll" && next != node->value) {
    document.invalidateLayout();
  }
  node->value = next;
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
  if (disabled)
    cancelSubtreeInteraction(document, id);
  return true;
}

bool UiStateController::setVisible(UiDocument &document,
                                   const std::string_view id,
                                   const bool visible) const {
  UiNode *node = find(document, id);
  if (node == nullptr)
    return false;
  if (node->visible != visible) {
    document.invalidateLayout();
  }
  node->visible = visible;
  if (!visible)
    cancelSubtreeInteraction(document, id);
  return true;
}

} // namespace demi::runtime::ui
