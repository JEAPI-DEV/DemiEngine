#include "editor/EditorHudHierarchy.h"

#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>

namespace demi::editor {

std::vector<EditorHudHierarchyNode>
editorHudHierarchy(const runtime::ui::UiDocument &document) {
  std::vector<EditorHudHierarchyNode> result;
  result.reserve(document.nodes.size());
  for (const runtime::ui::UiNode &node : document.nodes) {
    std::string label = node.id == "ui_root" ? "HUD Root" : node.id;
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

std::optional<std::string>
editorUiPrefabReference(const std::filesystem::path &projectDirectory,
                        const std::filesystem::path &source) {
  if (!isUiPrefabFile(source))
    return std::nullopt;
  std::error_code error;
  std::filesystem::path relative =
      std::filesystem::relative(source, projectDirectory / "ui", error);
  if (error || relative.empty() || relative.is_absolute())
    return std::nullopt;
  for (const std::filesystem::path &part : relative)
    if (part == "..")
      return std::nullopt;
  constexpr std::string_view Suffix = ".ui.prefab.json";
  std::string value = relative.generic_string();
  if (!value.ends_with(Suffix))
    return std::nullopt;
  value.resize(value.size() - Suffix.size());
  return value.empty() ? std::nullopt
                       : std::optional<std::string>("ui-prefab://" + value);
}

} // namespace demi::editor
