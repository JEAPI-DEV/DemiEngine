#include "demi/runtime/ui/UiLayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace demi::runtime::ui {
namespace {

float clampDimension(float value, float minimum, float maximum) {
  value = std::max(value, minimum);
  return maximum > 0.0F ? std::min(value, maximum) : value;
}

Rect anchoredRect(const UiNode &node, const Rect &parent) {
  const auto &layout = node.layout;
  const float left = parent.x + parent.width * layout.anchorMin.x +
                     layout.position.x + layout.margin.left;
  const float top = parent.y + parent.height * layout.anchorMin.y +
                    layout.position.y + layout.margin.top;
  float width = layout.size.x;
  float height = layout.size.y;
  if (layout.anchorMax.x > layout.anchorMin.x) {
    width = parent.width * (layout.anchorMax.x - layout.anchorMin.x) -
            layout.margin.left - layout.margin.right;
  }
  if (layout.anchorMax.y > layout.anchorMin.y) {
    height = parent.height * (layout.anchorMax.y - layout.anchorMin.y) -
             layout.margin.top - layout.margin.bottom;
  }
  return {left, top, clampDimension(width, layout.minSize.x, layout.maxSize.x),
          clampDimension(height, layout.minSize.y, layout.maxSize.y)};
}

} // namespace

void UiLayoutEngine::layout(UiDocument &document,
                            const Vec2 viewportSize) const {
  std::unordered_map<std::string, std::vector<std::size_t>> children;
  for (std::size_t index = 0; index < document.nodes.size(); ++index) {
    children[document.nodes[index].parent].push_back(index);
  }

  const Rect viewport{0.0F, 0.0F, std::max(viewportSize.x, 1.0F),
                      std::max(viewportSize.y, 1.0F)};
  std::function<void(std::size_t, const Rect &)> resolve =
      [&](const std::size_t index, const Rect &parentRect) {
        UiNode &node = document.nodes[index];
        node.resolved = anchoredRect(node, parentRect);
        auto found = children.find(node.id);
        if (found == children.end()) {
          return;
        }
        const Rect content{
            node.resolved.x + node.layout.padding.left,
            node.resolved.y + node.layout.padding.top -
                (node.type == "scroll" ? std::max(node.value, 0.0F) : 0.0F),
            std::max(0.0F, node.resolved.width - node.layout.padding.left -
                               node.layout.padding.right),
            std::max(0.0F, node.resolved.height - node.layout.padding.top -
                               node.layout.padding.bottom)};
        const auto &childIndexes = found->second;
        std::size_t flowOrder = 0;
        float flowOffset = 0.0F;
        for (const std::size_t childIndex : childIndexes) {
          UiNode &child = document.nodes[childIndex];
          if (!child.visible)
            continue;
          Rect slot = content;
          if (child.type == "modal") {
            slot.x += (content.width - child.layout.size.x) * 0.5F;
            slot.y += (content.height - child.layout.size.y) * 0.5F;
            slot.width = child.layout.size.x;
            slot.height = child.layout.size.y;
          } else if (node.layout.direction == LayoutDirection::Row) {
            slot.x += flowOffset;
            slot.width = child.layout.size.x;
            if (node.layout.alignment == Alignment::Center)
              slot.y += (content.height - child.layout.size.y) * 0.5F;
            else if (node.layout.alignment == Alignment::End)
              slot.y += content.height - child.layout.size.y;
            if (node.layout.alignment != Alignment::Stretch)
              slot.height = child.layout.size.y;
            flowOffset += child.layout.size.x + node.layout.gap;
          } else if (node.layout.direction == LayoutDirection::Column) {
            slot.y += flowOffset;
            slot.height = child.layout.size.y;
            if (node.layout.alignment == Alignment::Center)
              slot.x += (content.width - child.layout.size.x) * 0.5F;
            else if (node.layout.alignment == Alignment::End)
              slot.x += content.width - child.layout.size.x;
            if (node.layout.alignment != Alignment::Stretch)
              slot.width = child.layout.size.x;
            flowOffset += child.layout.size.y + node.layout.gap;
          } else if (node.layout.direction == LayoutDirection::Grid) {
            const int columns = std::max(node.layout.columns, 1);
            const int column = static_cast<int>(flowOrder) % columns;
            const int row = static_cast<int>(flowOrder) / columns;
            const float cellWidth =
                (content.width - node.layout.gap * (columns - 1)) / columns;
            slot.x += column * (cellWidth + node.layout.gap);
            slot.y += row * (child.layout.size.y + node.layout.gap);
            slot.width = cellWidth;
          }
          resolve(childIndex, slot);
          if (child.type != "modal")
            ++flowOrder;
        }
      };

  for (const std::size_t rootIndex : children[""]) {
    resolve(rootIndex, viewport);
  }
}

} // namespace demi::runtime::ui
