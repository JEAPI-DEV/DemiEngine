#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <filesystem>
#include <optional>
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
[[nodiscard]] std::optional<std::string>
editorUiPrefabReference(const std::filesystem::path &projectDirectory,
                        const std::filesystem::path &source);

} // namespace demi::editor
