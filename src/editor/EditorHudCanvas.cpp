#include "editor/EditorHudCanvas.h"
#include "demi/runtime/ui/UiPresentation.h"

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
  for (const auto &item : runtime::ui::buildUiPresentation(document)) {
    const auto &node = *item.node;
    if (item.visible && hasEditableVisual(node) &&
        runtime::ui::uiPointInsideScrollClip(document, node, authoredPoint) &&
        editorHudRectContains(editorHudEditableRect(node), authoredPoint))
      picked = &node;
  }
  return picked;
}

const runtime::ui::UiNode *
pickEditorHudDropParent(const runtime::ui::UiDocument &document,
                        runtime::Vec2 point) {
  const runtime::ui::UiNode *picked = nullptr;
  for (const auto &item : runtime::ui::buildUiPresentation(document)) {
    const auto &node = *item.node;
    const bool container = node.type == "container" || node.type == "panel" ||
                           node.type == "scroll" || node.type == "list" ||
                           node.type == "modal";
    if (item.visible && container &&
        editorHudRectContains(node.resolved, point) &&
        runtime::ui::uiPointInsideScrollClip(document, node, point))
      picked = &node;
  }
  return picked;
}

} // namespace demi::editor
