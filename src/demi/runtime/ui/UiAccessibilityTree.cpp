#include "demi/runtime/ui/UiAccessibilityTree.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {

UiAccessibilityRole roleFor(const std::string_view type) {
  if (type == "container" || type == "panel")
    return UiAccessibilityRole::Group;
  if (type == "label" || type == "text")
    return UiAccessibilityRole::StaticText;
  if (type == "image")
    return UiAccessibilityRole::Image;
  if (type == "button" || type == "virtual_button")
    return UiAccessibilityRole::Button;
  if (type == "toggle")
    return UiAccessibilityRole::CheckBox;
  if (type == "slider")
    return UiAccessibilityRole::Slider;
  if (type == "text_input")
    return UiAccessibilityRole::TextField;
  if (type == "scroll")
    return UiAccessibilityRole::ScrollArea;
  if (type == "list")
    return UiAccessibilityRole::List;
  if (type == "progress")
    return UiAccessibilityRole::ProgressBar;
  if (type == "modal")
    return UiAccessibilityRole::Dialog;
  if (type == "virtual_stick")
    return UiAccessibilityRole::Joystick;
  return UiAccessibilityRole::Generic;
}

std::string labelFor(const UiNode &node, const UiAccessibilityRole role) {
  if (!node.accessibilityLabel.empty())
    return node.accessibilityLabel;
  if (role == UiAccessibilityRole::StaticText ||
      role == UiAccessibilityRole::Button ||
      role == UiAccessibilityRole::CheckBox)
    return node.text;
  if (role == UiAccessibilityRole::TextField)
    return node.placeholder;
  return {};
}

bool isSemanticallyExposed(const UiNode &node, const UiAccessibilityRole role,
                           const std::string_view label) {
  if (node.accessibilityHidden)
    return false;
  if (role == UiAccessibilityRole::Group ||
      role == UiAccessibilityRole::Generic ||
      role == UiAccessibilityRole::Image ||
      role == UiAccessibilityRole::StaticText)
    return !label.empty();
  return true;
}

Rect safeBounds(const Rect bounds) {
  return {
      .x = std::isfinite(bounds.x) ? bounds.x : 0.0F,
      .y = std::isfinite(bounds.y) ? bounds.y : 0.0F,
      .width =
          std::isfinite(bounds.width) ? std::max(bounds.width, 0.0F) : 0.0F,
      .height =
          std::isfinite(bounds.height) ? std::max(bounds.height, 0.0F) : 0.0F,
  };
}

Rect intersectBounds(const Rect first, const Rect second) {
  const float left = std::max(first.x, second.x);
  const float top = std::max(first.y, second.y);
  const float right = std::min(first.x + first.width, second.x + second.width);
  const float bottom =
      std::min(first.y + first.height, second.y + second.height);
  return {.x = left,
          .y = top,
          .width = std::max(right - left, 0.0F),
          .height = std::max(bottom - top, 0.0F)};
}

} // namespace

std::vector<UiAccessibilityNode>
UiAccessibilityTree::snapshot(const UiDocument &document) {
  std::unordered_map<std::string, const UiNode *> nodesById;
  nodesById.reserve(document.nodes.size());
  for (const UiNode &node : document.nodes)
    nodesById.try_emplace(node.id, &node);

  const auto isAvailable = [&](const UiNode &node) {
    std::unordered_set<std::string> visited;
    const UiNode *current = &node;
    while (current != nullptr && visited.insert(current->id).second) {
      if (!current->visible || current->accessibilityHidden)
        return false;
      const auto parent = nodesById.find(current->parent);
      current = parent == nodesById.end() ? nullptr : parent->second;
    }
    return current == nullptr;
  };
  const auto isDisabled = [&](const UiNode &node) {
    std::unordered_set<std::string> visited;
    const UiNode *current = &node;
    while (current != nullptr && visited.insert(current->id).second) {
      if (current->disabled)
        return true;
      const auto parent = nodesById.find(current->parent);
      current = parent == nodesById.end() ? nullptr : parent->second;
    }
    return false;
  };

  std::unordered_set<std::string> exposedIds;
  exposedIds.reserve(document.nodes.size());
  for (const UiNode &node : document.nodes) {
    const auto canonical = nodesById.find(node.id);
    if (canonical == nodesById.end() || canonical->second != &node)
      continue;
    const UiAccessibilityRole role = roleFor(node.type);
    if (isAvailable(node) &&
        isSemanticallyExposed(node, role, labelFor(node, role)))
      exposedIds.insert(node.id);
  }

  const auto exposedParent = [&](const UiNode &node) {
    std::unordered_set<std::string> visited;
    std::string parentId = node.parent;
    while (!parentId.empty() && visited.insert(parentId).second) {
      if (exposedIds.contains(parentId))
        return parentId;
      const auto parent = nodesById.find(parentId);
      if (parent == nodesById.end())
        break;
      parentId = parent->second->parent;
    }
    return std::string{};
  };
  const auto clippedBounds = [&](const UiNode &node) {
    Rect bounds = safeBounds(node.resolved);
    std::unordered_set<std::string> visited;
    std::string parentId = node.parent;
    while (!parentId.empty() && visited.insert(parentId).second) {
      const auto parent = nodesById.find(parentId);
      if (parent == nodesById.end())
        break;
      if (parent->second->type == "scroll")
        bounds = intersectBounds(bounds, safeBounds(parent->second->resolved));
      parentId = parent->second->parent;
    }
    return bounds;
  };

  std::vector<UiAccessibilityNode> result;
  result.reserve(exposedIds.size());
  std::unordered_set<std::string> emittedIds;
  for (const UiNode &node : document.nodes) {
    if (!exposedIds.contains(node.id) || !emittedIds.insert(node.id).second)
      continue;
    const UiAccessibilityRole role = roleFor(node.type);
    const Rect bounds = clippedBounds(node);
    result.push_back({
        .id = node.id,
        .parent = exposedParent(node),
        .role = role,
        .label = labelFor(node, role),
        .description = node.accessibilityDescription,
        .valueText =
            role == UiAccessibilityRole::TextField ? node.text : std::string{},
        .bounds = bounds,
        .value = node.value,
        .minimum = node.minimum,
        .maximum = node.maximum,
        .focused = document.focusedId == node.id,
        .disabled = isDisabled(node),
        .checked = node.checked,
        .focusable = node.focusable,
        .offscreen = bounds.width <= 0.0F || bounds.height <= 0.0F,
    });
  }
  return result;
}

std::string_view uiAccessibilityRoleName(const UiAccessibilityRole role) {
  switch (role) {
  case UiAccessibilityRole::Generic:
    return "generic";
  case UiAccessibilityRole::Group:
    return "group";
  case UiAccessibilityRole::StaticText:
    return "static_text";
  case UiAccessibilityRole::Image:
    return "image";
  case UiAccessibilityRole::Button:
    return "button";
  case UiAccessibilityRole::CheckBox:
    return "check_box";
  case UiAccessibilityRole::Slider:
    return "slider";
  case UiAccessibilityRole::TextField:
    return "text_field";
  case UiAccessibilityRole::ScrollArea:
    return "scroll_area";
  case UiAccessibilityRole::List:
    return "list";
  case UiAccessibilityRole::ProgressBar:
    return "progress_bar";
  case UiAccessibilityRole::Dialog:
    return "dialog";
  case UiAccessibilityRole::Joystick:
    return "joystick";
  }
  return "generic";
}

} // namespace demi::runtime::ui
