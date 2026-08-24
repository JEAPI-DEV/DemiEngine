#include "editor/EditorHudHierarchy.h"

#include <algorithm>

namespace demi::editor {

std::vector<EditorHudHierarchyNode>
editorHudHierarchy(const runtime::ui::UiDocument &document) {
  std::vector<EditorHudHierarchyNode> result;
  result.reserve(document.nodes.size());
  for (const runtime::ui::UiNode &node : document.nodes) {
    std::string label = node.id;
    if (label.empty())
      label = node.type.empty() ? "UI Node" : node.type;
    result.push_back({.id = node.id,
                      .parent = node.parent,
                      .label = std::move(label),
                      .type = node.type,
                      .visible = node.visible});
  }
  return result;
}

const runtime::ui::UiNode *
findEditorHudNode(const runtime::ui::UiDocument &document,
                  const std::string_view id) {
  const auto found =
      std::ranges::find(document.nodes, id, &runtime::ui::UiNode::id);
  return found == document.nodes.end() ? nullptr : &*found;
}

} // namespace demi::editor
