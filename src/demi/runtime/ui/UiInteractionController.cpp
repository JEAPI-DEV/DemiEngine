#include "demi/runtime/ui/UiInteractionController.h"

#include "demi/runtime/ui/UiEventQueue.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace demi::runtime::ui {
namespace {

bool available(const UiDocument &document, const UiNode &node) {
  if (!node.visible || node.disabled)
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

bool interactive(const UiDocument &document, const UiNode &node) {
  return node.focusable && available(document, node);
}

bool hoveredByAnotherPointer(const UiDocument &document,
                             const std::string_view id,
                             const std::int64_t excludedPointer) {
  return std::ranges::any_of(document.pointerHoverIds, [&](const auto &hover) {
    return hover.first != excludedPointer && hover.second == id;
  });
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

UiNode *find(UiDocument &document, const std::string_view id) {
  const auto node = std::ranges::find(document.nodes, id, &UiNode::id);
  return node == document.nodes.end() ? nullptr : &*node;
}

UiNode *hitTest(UiDocument &document, const Vec2 position) {
  const std::string modal = activeModal(document);
  for (auto node = document.nodes.rbegin(); node != document.nodes.rend();
       ++node) {
    if (interactive(document, *node) && belongsTo(document, *node, modal) &&
        contains(node->resolved, position))
      return &*node;
  }
  return nullptr;
}

UiNode *scrollTarget(UiDocument &document, const Vec2 position) {
  const std::string modal = activeModal(document);
  for (auto node = document.nodes.rbegin(); node != document.nodes.rend();
       ++node) {
    if (!available(document, *node) || !belongsTo(document, *node, modal) ||
        !contains(node->resolved, position))
      continue;
    UiNode *candidate = &*node;
    while (candidate != nullptr) {
      if (candidate->type == "scroll")
        return candidate;
      candidate = find(document, candidate->parent);
    }
  }
  return hitTest(document, position);
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

void setFocus(UiDocument &document, const std::string_view id,
              const std::string_view source) {
  if (document.focusedId == id)
    return;
  if (const UiNode *previous = find(document, document.focusedId))
    UiEventQueue::push(document,
                       eventFor(*previous, UiEventType::FocusLost, source));
  document.focusedId = id;
  if (const UiNode *focused = find(document, document.focusedId))
    UiEventQueue::push(document,
                       eventFor(*focused, UiEventType::FocusGained, source));
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
    setFocus(document, {}, "keyboard");
    return false;
  }
  const auto current = std::ranges::find_if(candidates, [&](std::size_t i) {
    return document.nodes[i].id == document.focusedId;
  });
  std::size_t position =
      current == candidates.end()
          ? (reverse ? candidates.size() - 1 : 0)
          : static_cast<std::size_t>(current - candidates.begin());
  if (current != candidates.end())
    position = reverse ? (position + candidates.size() - 1) % candidates.size()
                       : (position + 1) % candidates.size();
  setFocus(document, document.nodes[candidates[position]].id, "keyboard");
  return true;
}

bool UiInteractionController::movePointer(UiDocument &document,
                                          const std::int64_t pointerId,
                                          const Vec2 position,
                                          const std::string_view source) const {
  document.pointerPositions[pointerId] = position;
  UiNode *target = hitTest(document, position);
  const std::string next = target == nullptr ? std::string{} : target->id;
  const auto previous = document.pointerHoverIds.find(pointerId);
  const std::string prior = previous == document.pointerHoverIds.end()
                                ? std::string{}
                                : previous->second;
  if (prior == next)
    return false;
  if (UiNode *node = find(document, prior)) {
    node->hovered = hoveredByAnotherPointer(document, prior, pointerId);
    UiEvent event = eventFor(*node, UiEventType::PointerExit, source);
    event.pointerId = pointerId;
    event.position = position;
    event.relatedId = next;
    UiEventQueue::push(document, std::move(event));
  }
  if (target != nullptr) {
    target->hovered = true;
    UiEvent event = eventFor(*target, UiEventType::PointerEnter, source);
    event.pointerId = pointerId;
    event.position = position;
    event.relatedId = prior;
    UiEventQueue::push(document, std::move(event));
    document.pointerHoverIds[pointerId] = target->id;
  } else {
    document.pointerHoverIds.erase(pointerId);
  }
  return true;
}

bool UiInteractionController::capturePointer(UiDocument &document,
                                             const Vec2 position) const {
  return capturePointer(document, 0, position, "mouse");
}

bool UiInteractionController::capturePointer(
    UiDocument &document, const std::int64_t pointerId, const Vec2 position,
    const std::string_view source) const {
  (void)movePointer(document, pointerId, position, source);
  UiNode *target = hitTest(document, position);
  if (target == nullptr)
    return false;
  if (pointerCaptured(document, pointerId))
    releasePointer(document, pointerId, position, true, source);
  document.pointerCaptures[pointerId] = target->id;
  document.pointerPressPositions[pointerId] = position;
  document.draggingPointers.erase(pointerId);
  setFocus(document, target->id, source);
  UiEvent event = eventFor(*target, UiEventType::Press, source);
  event.pointerId = pointerId;
  event.position = position;
  UiEventQueue::push(document, std::move(event));
  return true;
}

bool UiInteractionController::updatePointer(UiDocument &document,
                                            const Vec2 position) const {
  return updatePointer(document, 0, position, "mouse");
}

bool UiInteractionController::updatePointer(
    UiDocument &document, const std::int64_t pointerId, const Vec2 position,
    const std::string_view source) const {
  const Vec2 previous = document.pointerPositions.contains(pointerId)
                            ? document.pointerPositions.at(pointerId)
                            : position;
  (void)movePointer(document, pointerId, position, source);
  const auto capture = document.pointerCaptures.find(pointerId);
  if (capture == document.pointerCaptures.end())
    return false;
  UiNode *captured = find(document, capture->second);
  if (captured == nullptr)
    return false;

  const Vec2 delta{position.x - previous.x, position.y - previous.y};
  const Vec2 pressed = document.pointerPressPositions[pointerId];
  const float dragDistanceSquared =
      (position.x - pressed.x) * (position.x - pressed.x) +
      (position.y - pressed.y) * (position.y - pressed.y);
  if (!document.draggingPointers.contains(pointerId) &&
      dragDistanceSquared >= 9.0F) {
    document.draggingPointers.insert(pointerId);
    UiEvent start = eventFor(*captured, UiEventType::DragStart, source);
    start.pointerId = pointerId;
    start.position = position;
    start.delta = delta;
    UiEventQueue::push(document, std::move(start));
  }
  if (document.draggingPointers.contains(pointerId) &&
      (delta.x != 0.0F || delta.y != 0.0F)) {
    UiEvent drag = eventFor(*captured, UiEventType::Drag, source);
    drag.pointerId = pointerId;
    drag.position = position;
    drag.delta = delta;
    UiEventQueue::push(document, std::move(drag));
  }

  if (captured->type != "slider" || captured->resolved.width <= 0.0F)
    return document.draggingPointers.contains(pointerId);
  const float fraction =
      std::clamp((position.x - captured->resolved.x) / captured->resolved.width,
                 0.0F, 1.0F);
  const float value =
      captured->minimum + fraction * (captured->maximum - captured->minimum);
  if (value == captured->value)
    return false;
  captured->value = value;
  UiEventQueue::valueChanged(document, *captured, source);
  return true;
}

bool UiInteractionController::scrollPointer(
    UiDocument &document, const std::int64_t pointerId, const Vec2 position,
    const Vec2 delta, const std::string_view source) const {
  (void)movePointer(document, pointerId, position, source);
  UiNode *target = scrollTarget(document, position);
  if (target == nullptr || (delta.x == 0.0F && delta.y == 0.0F))
    return false;
  UiEvent event = eventFor(*target, UiEventType::Scroll, source);
  event.pointerId = pointerId;
  event.position = position;
  event.delta = delta;
  UiEventQueue::push(document, std::move(event));
  return true;
}

void UiInteractionController::releasePointer(UiDocument &document) const {
  releasePointer(document, 0);
}

void UiInteractionController::releasePointer(
    UiDocument &document, const std::int64_t pointerId) const {
  const Vec2 position = document.pointerPositions.contains(pointerId)
                            ? document.pointerPositions.at(pointerId)
                            : Vec2{};
  releasePointer(document, pointerId, position, false,
                 pointerId == 0 ? "mouse" : "touch");
}

void UiInteractionController::releasePointer(
    UiDocument &document, const std::int64_t pointerId, const Vec2 position,
    const bool cancelled, const std::string_view source) const {
  const auto capture = document.pointerCaptures.find(pointerId);
  if (capture == document.pointerCaptures.end())
    return;
  const std::string capturedId = capture->second;
  if (UiNode *captured = find(document, capturedId)) {
    UiEvent release = eventFor(*captured, UiEventType::Release, source);
    release.pointerId = pointerId;
    release.position = position;
    release.cancelled = cancelled;
    UiEventQueue::push(document, std::move(release));
    if (document.draggingPointers.contains(pointerId)) {
      UiEvent end = eventFor(*captured, UiEventType::DragEnd, source);
      end.pointerId = pointerId;
      end.position = position;
      end.cancelled = cancelled;
      UiEventQueue::push(document, std::move(end));
      if (!cancelled) {
        if (UiNode *target = hitTest(document, position)) {
          UiEvent drop = eventFor(*target, UiEventType::Drop, source);
          drop.relatedId = capturedId;
          drop.pointerId = pointerId;
          drop.position = position;
          UiEventQueue::push(document, std::move(drop));
        }
      }
    }
    if (cancelled) {
      UiEvent cancelEvent = eventFor(*captured, UiEventType::Cancel, source);
      cancelEvent.pointerId = pointerId;
      cancelEvent.position = position;
      cancelEvent.cancelled = true;
      UiEventQueue::push(document, std::move(cancelEvent));
    }
  }
  document.pointerCaptures.erase(pointerId);
  document.pointerPressPositions.erase(pointerId);
  document.draggingPointers.erase(pointerId);
}

void UiInteractionController::cancel(UiDocument &document,
                                     const std::string_view source) const {
  const std::string focusedId = document.focusedId;
  const bool focusedWasCaptured =
      std::ranges::any_of(document.pointerCaptures, [&](const auto &capture) {
        return capture.second == focusedId;
      });
  std::vector<std::int64_t> pointers;
  pointers.reserve(document.pointerCaptures.size());
  for (const auto &[pointerId, _] : document.pointerCaptures)
    pointers.push_back(pointerId);
  for (const std::int64_t pointerId : pointers) {
    const Vec2 position = document.pointerPositions.contains(pointerId)
                              ? document.pointerPositions.at(pointerId)
                              : Vec2{};
    releasePointer(document, pointerId, position, true, source);
  }
  if (!focusedWasCaptured) {
    if (UiNode *focused = find(document, document.focusedId))
      UiEventQueue::push(document,
                         eventFor(*focused, UiEventType::Cancel, source));
  }
}

bool UiInteractionController::pointerCaptured(
    const UiDocument &document, const std::int64_t pointerId) const {
  return document.pointerCaptures.contains(pointerId);
}

std::optional<std::string>
UiInteractionController::activateFocused(UiDocument &document,
                                         const std::string_view source) const {
  UiNode *focused = find(document, document.focusedId);
  if (focused == nullptr || !interactive(document, *focused))
    return std::nullopt;
  if (focused->type == "toggle") {
    focused->checked = !focused->checked;
    UiEventQueue::valueChanged(document, *focused, source);
  }
  if (focused->type != "text_input" || source == "keyboard" ||
      source == "controller")
    UiEventQueue::push(document,
                       eventFor(*focused, UiEventType::Submit, source));
  if (focused->action.empty())
    return std::nullopt;
  return focused->action;
}

} // namespace demi::runtime::ui
