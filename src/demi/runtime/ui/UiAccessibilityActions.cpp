#include "demi/runtime/ui/UiAccessibilityActions.h"

#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiEventQueue.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiStateController.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::ui {
namespace {
UiEvent eventFor(const UiNode &node, const UiEventType type) {
  return {.type = type,
          .id = node.id,
          .action = node.action,
          .text = node.text,
          .source = "accessibility",
          .value = node.value,
          .checked = node.checked};
}

bool available(const UiDocument &document, const UiNode &node) {
  if (!node.visible || node.disabled || node.accessibilityHidden)
    return false;
  std::string parent = node.parent;
  std::size_t guard = 0;
  while (!parent.empty() && guard++ <= document.nodes.size()) {
    const UiNode *value = UiStateController{}.find(document, parent);
    if (value == nullptr || !value->visible || value->disabled ||
        value->accessibilityHidden)
      return false;
    parent = value->parent;
  }
  return true;
}
} // namespace

UiAccessibilityActionResult UiAccessibilityActions::perform(
    UiDocument &document, const UiAccessibilityAction &request) const {
  UiNode *node = UiStateController{}.find(document, request.nodeId);
  if (node == nullptr)
    return {.error = "Accessibility action targets a missing UI node."};
  if (!available(document, *node))
    return {.error = "Accessibility action targets an unavailable UI node."};

  switch (request.type) {
  case UiAccessibilityActionType::Focus: {
    if (!node->focusable)
      return {.error = "Accessibility focus requires a focusable node."};
    if (document.focusedId != node->id) {
      if (UiNode *previous =
              UiStateController{}.find(document, document.focusedId))
        UiEventQueue::push(document,
                           eventFor(*previous, UiEventType::FocusLost));
      document.focusedId = node->id;
      UiEventQueue::push(document, eventFor(*node, UiEventType::FocusGained));
    }
    break;
  }
  case UiAccessibilityActionType::Activate:
    if (!node->focusable)
      return {.error = "Accessibility activation requires an interactive node."};
    if (node->type == "toggle") {
      node->checked = !node->checked;
      UiEventQueue::valueChanged(document, *node, "accessibility");
    }
    UiEventQueue::push(document, eventFor(*node, UiEventType::Submit));
    return {.handled = true, .action = node->action};
  case UiAccessibilityActionType::Increment:
  case UiAccessibilityActionType::Decrement: {
    if (node->type != "slider" && node->type != "progress")
      return {.error = "Accessibility increment requires a ranged node."};
    const float span = std::max(node->maximum - node->minimum, 0.0F);
    const float step = span > 0.0F ? span / 10.0F : 1.0F;
    const float sign = request.type == UiAccessibilityActionType::Increment
                           ? 1.0F
                           : -1.0F;
    node->value = std::clamp(node->value + sign * step, node->minimum,
                             node->maximum);
    UiEventQueue::valueChanged(document, *node, "accessibility");
    break;
  }
  case UiAccessibilityActionType::SetValue:
    if ((node->type != "slider" && node->type != "progress") ||
        !std::isfinite(request.value))
      return {.error = "Accessibility set-value requires a finite ranged value."};
    node->value = std::clamp(request.value, node->minimum, node->maximum);
    UiEventQueue::valueChanged(document, *node, "accessibility");
    break;
  case UiAccessibilityActionType::SetText:
    if (node->type != "text_input")
      return {.error = "Accessibility set-text requires a text input."};
    if (!request.text.empty() &&
        TextLayoutEngine::graphemeCount(request.text) == 0)
      return {.error = "Accessibility text must be valid UTF-8."};
    node->text = request.text;
    node->textEdit = {};
    node->textEdit.caret = TextLayoutEngine::graphemeCount(node->text);
    node->textEdit.anchor = node->textEdit.caret;
    UiEventQueue::valueChanged(document, *node, "accessibility");
    break;
  case UiAccessibilityActionType::ScrollForward:
  case UiAccessibilityActionType::ScrollBackward: {
    if (node->type != "scroll")
      return {.error = "Accessibility scrolling requires a scroll node."};
    UiEvent event = eventFor(*node, UiEventType::Scroll);
    event.delta.y = request.type == UiAccessibilityActionType::ScrollForward
                        ? node->resolved.height
                        : -node->resolved.height;
    UiEventQueue::push(document, std::move(event));
    break;
  }
  }
  return {.handled = true};
}

} // namespace demi::runtime::ui
