#include "editor/EditorHudHierarchy.h"
#include "editor/EditorWorkspace.h"

#include <cassert>
#include <filesystem>

int main() {
  demi::runtime::ui::UiDocument document;
  document.nodes = {
      {.id = "ui_root", .type = "container", .visible = true},
      {.id = "score", .parent = "ui_root", .type = "label", .visible = true},
      {.id = "pause", .parent = "ui_root", .type = "panel", .visible = false}};
  const auto hierarchy = demi::editor::editorHudHierarchy(document);
  assert(hierarchy.size() == 3);
  assert(hierarchy[0].label == "HUD Root");
  assert(hierarchy[1].parent == "ui_root");
  assert(!hierarchy[2].visible);
  assert(demi::editor::findEditorHudNode(document, "score") ==
         &document.nodes[1]);
  assert(demi::editor::findEditorHudNode(document, "missing") == nullptr);

  demi::editor::EditorWorkspace workspace;
  std::string error;
  const std::filesystem::path root = DEMI_SOURCE_DIR;
  const auto prefabReference = demi::editor::editorUiPrefabReference(
      root / "examples/ui_showcase",
      root / "examples/ui_showcase/ui/menu_button.ui.prefab.json");
  assert(prefabReference == "ui-prefab://menu_button");
  assert(!demi::editor::editorUiPrefabReference(root / "examples/ui_showcase",
                                                root / "docs/runtime-ui.md"));
  assert(workspace.open(root / "examples/minimal_voxel", error));
  assert(workspace.authoredHudPath());
  assert(workspace.authoredHudPath()->filename() == "main.hud.json");
  assert(
      !demi::editor::editorHudHierarchy(workspace.project().world.ui).empty());
}
