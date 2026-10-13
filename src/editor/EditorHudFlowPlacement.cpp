#include "editor/EditorHudFlowPlacement.h"
#include <algorithm>
#include <vector>

namespace demi::editor {
EditorHudFlowPlacement editorHudFlowPlacement(
    const runtime::ui::UiDocument &preview, std::string_view parentId,
    const nlohmann::json &children, std::optional<runtime::Vec2> position) {
  using runtime::ui::LayoutDirection;
  const auto parent =
      std::ranges::find(preview.nodes, parentId, &runtime::ui::UiNode::id);
  EditorHudFlowPlacement result{.index = children.size()};
  if (parent == preview.nodes.end() ||
      parent->layout.direction == LayoutDirection::None)
    return result;
  result.flow = true;
  if (!position)
    return result;
  const runtime::Vec2 point{
      parent->resolved.x + parent->layout.padding.left + position->x,
      parent->resolved.y + parent->layout.padding.top + position->y};
  struct Child {
    std::size_t index;
    runtime::ui::Rect bounds;
  };
  std::vector<Child> visible;
  for (std::size_t i = 0; i < children.size(); ++i) {
    const auto id = children[i].value("id", std::string{});
    const auto node =
        std::ranges::find(preview.nodes, id, &runtime::ui::UiNode::id);
    if (node != preview.nodes.end() && node->parent == parent->id &&
        node->visible && node->type != "modal")
      visible.push_back({i, node->resolved});
  }
  if (parent->layout.direction == LayoutDirection::Grid) {
    const auto columns =
        static_cast<std::size_t>(std::max(parent->layout.columns, 1));
    for (std::size_t row = 0; row < visible.size(); row += columns) {
      const auto end = std::min(row + columns, visible.size());
      float bottom = visible[row].bounds.y + visible[row].bounds.height;
      for (auto i = row + 1; i < end; ++i)
        bottom =
            std::max(bottom, visible[i].bounds.y + visible[i].bounds.height);
      if (point.y > bottom + parent->layout.gap * 0.5F)
        continue;
      for (auto i = row; i < end; ++i)
        if (point.x < visible[i].bounds.x + visible[i].bounds.width * 0.5F)
          return {.flow = true, .index = visible[i].index};
      return {.flow = true, .index = visible[end - 1].index + 1};
    }
  } else {
    for (const auto &child : visible) {
      const bool before =
          parent->layout.direction == LayoutDirection::Row
              ? point.x < child.bounds.x + child.bounds.width * 0.5F
              : point.y < child.bounds.y + child.bounds.height * 0.5F;
      if (before)
        return {.flow = true, .index = child.index};
    }
  }
  return result;
}
} // namespace demi::editor
