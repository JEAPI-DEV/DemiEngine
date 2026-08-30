#include "editor/EditorHudCanvas.h"

#include <algorithm>

namespace demi::editor {
namespace {

bool hasEditableVisual(const runtime::ui::UiNode &node) {
  return node.type != "container" || node.backgroundColor.a > 0.0F ||
         (node.borderWidth > 0.0F && node.borderColor.a > 0.0F) ||
         !node.text.empty() || !node.texture.empty();
}

} // namespace

runtime::ui::Rect editorHudEditableRect(const runtime::ui::UiNode &node) {
  runtime::ui::Rect result = node.resolved;
  if (result.width <= 0.0F && !node.text.empty())
    result.width = std::max(node.fontSize * 0.55F * node.text.size(), 24.0F);
  if (result.height <= 0.0F && !node.text.empty())
    result.height = std::max(node.fontSize * 1.25F, 18.0F);
  return result;
}

bool editorHudRectContains(const runtime::ui::Rect rect,
                           const runtime::Vec2 point) {
  return point.x >= rect.x && point.y >= rect.y &&
         point.x <= rect.x + rect.width && point.y <= rect.y + rect.height;
}

const runtime::ui::UiNode *
pickEditorHudNode(const runtime::ui::UiDocument &document,
                  const runtime::Vec2 authoredPoint) {
  const runtime::ui::UiNode *picked = nullptr;
  for (const runtime::ui::UiNode &node : document.nodes) {
    if (!node.visible || !hasEditableVisual(node) ||
        !editorHudRectContains(editorHudEditableRect(node), authoredPoint))
      continue;
    if (picked == nullptr || node.layer >= picked->layer)
      picked = &node;
  }
  return picked;
}

} // namespace demi::editor
