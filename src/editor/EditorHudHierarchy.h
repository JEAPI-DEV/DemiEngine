#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

struct EditorHudHierarchyNode {
  std::string id;
  std::string parent;
  std::string label;
  std::string type;
  bool visible = true;
};

[[nodiscard]] std::vector<EditorHudHierarchyNode>
editorHudHierarchy(const runtime::ui::UiDocument &document);
[[nodiscard]] const runtime::ui::UiNode *
findEditorHudNode(const runtime::ui::UiDocument &document, std::string_view id);

} // namespace demi::editor
