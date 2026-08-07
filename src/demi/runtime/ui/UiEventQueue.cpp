#include "demi/runtime/ui/UiEventQueue.h"

#include <algorithm>
#include <ranges>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {

const UiNode *find(const UiDocument &document, const std::string_view id) {
  const auto node = std::ranges::find(document.nodes, id, &UiNode::id);
  return node == document.nodes.end() ? nullptr : &*node;
}

UiNode *find(UiDocument &document, const std::string_view id) {
  const auto node = std::ranges::find(document.nodes, id, &UiNode::id);
  return node == document.nodes.end() ? nullptr : &*node;
}

bool belongsTo(const UiDocument &document, std::string id,
               const std::string_view ancestor) {
  std::unordered_set<std::string> visited;
  while (!id.empty() && visited.insert(id).second) {
    if (id == ancestor)
      return true;
    const UiNode *node = find(document, id);
    if (node == nullptr)
      return false;
    id = node->parent;
  }
  return false;
}

UiEvent eventFor(const UiNode &node, const UiEventType type,
                 const std::string_view source) {
  UiEvent event;
  event.type = type;
  event.id = node.id;
  event.action = node.action;
  event.text = node.text;
  event.source = source;
  event.value = node.value;
  event.checked = node.checked;
  return event;
}

} // namespace

void UiEventQueue::push(UiDocument &document, UiEvent event) {
  if (!event.id.empty())
    document.events.push_back(std::move(event));
}

std::vector<UiEvent> UiEventQueue::take(UiDocument &document) {
  std::vector<UiEvent> events;
  events.swap(document.events);
  return events;
}

void UiEventQueue::valueChanged(UiDocument &document, const UiNode &node,
                                const std::string_view source) {
  push(document, eventFor(node, UiEventType::ValueChanged, source));
}

void UiEventQueue::cancelSubtree(UiDocument &document,
                                 const std::string_view root,
                                 const std::string_view source) {
  const auto mouseCapture = document.pointerCaptures.find(0);
  const bool mouseCaptureInSubtree =
      belongsTo(document, document.pointerCaptureId, root) ||
      (mouseCapture != document.pointerCaptures.end() &&
       belongsTo(document, mouseCapture->second, root));
  const bool focusedWasCaptured =
      std::ranges::any_of(document.pointerCaptures, [&](const auto &capture) {
        return capture.second == document.focusedId &&
               belongsTo(document, capture.second, root);
      });
  if (belongsTo(document, document.focusedId, root)) {
    if (const UiNode *focused = find(document, document.focusedId)) {
      if (!focusedWasCaptured)
        push(document, eventFor(*focused, UiEventType::Cancel, source));
      push(document, eventFor(*focused, UiEventType::FocusLost, source));
    }
    document.focusedId.clear();
  }

  for (const auto &[pointerId, capturedId] : document.pointerCaptures) {
    if (!belongsTo(document, capturedId, root))
      continue;
    if (const UiNode *captured = find(document, capturedId)) {
      UiEvent cancel = eventFor(*captured, UiEventType::Cancel, source);
      cancel.pointerId = pointerId;
      cancel.position = document.pointerPositions[pointerId];
      cancel.cancelled = true;
      push(document, std::move(cancel));
      if (document.draggingPointers.contains(pointerId)) {
        UiEvent dragEnd = eventFor(*captured, UiEventType::DragEnd, source);
        dragEnd.pointerId = pointerId;
        dragEnd.position = document.pointerPositions[pointerId];
        dragEnd.cancelled = true;
        push(document, std::move(dragEnd));
      }
    }
  }

  for (const auto &[pointerId, hoveredId] : document.pointerHoverIds) {
    if (!belongsTo(document, hoveredId, root))
      continue;
    if (UiNode *hovered = find(document, hoveredId)) {
      hovered->hovered = false;
      UiEvent pointerExit =
          eventFor(*hovered, UiEventType::PointerExit, source);
      pointerExit.pointerId = pointerId;
      pointerExit.position = document.pointerPositions[pointerId];
      pointerExit.cancelled = true;
      push(document, std::move(pointerExit));
    }
  }

  std::erase_if(document.pointerCaptures, [&](const auto &capture) {
    return belongsTo(document, capture.second, root);
  });
  std::erase_if(document.pointerHoverIds, [&](const auto &hover) {
    return belongsTo(document, hover.second, root);
  });
  std::erase_if(document.pointerPositions, [&](const auto &position) {
    return !document.pointerCaptures.contains(position.first) &&
           !document.pointerHoverIds.contains(position.first);
  });
  std::erase_if(document.pointerPressPositions, [&](const auto &position) {
    return !document.pointerCaptures.contains(position.first);
  });
  std::erase_if(document.draggingPointers, [&](const auto pointerId) {
    return !document.pointerCaptures.contains(pointerId);
  });
  if (mouseCaptureInSubtree)
    document.pointerCaptureId.clear();
}

} // namespace demi::runtime::ui
