#include "editor/EditorWorkspace.h"

#include "demi/schema/Validation.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

std::filesystem::path createProject(const std::filesystem::path &root) {
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "scenes");
  write(root / "demi.project.json", R"({
  "format_version": 1,
  "name": "Editor Authoring Workflow",
  "main_scene": "scene://editor_workflow/main",
  "scenes": [
    {
      "id": "scene://editor_workflow/main",
      "path": "scenes/main.scene.json"
    }
  ],
  "scripting": {
    "language": "lua54",
    "modules": []
  }
})");
  write(root / "scenes/main.scene.json", R"({
  "format_version": 1,
  "id": "scene://editor_workflow/main",
  "name": "Editor Workflow Scene",
  "entities": [
    {
      "id": "root",
      "name": "Root",
      "components": {
        "Transform3D": {
          "position": [0.0, 0.0, 0.0]
        }
      }
    },
    {
      "id": "child",
      "name": "Child",
      "components": {
        "Transform3D": {
          "parent": "root",
          "position": [1.0, 0.0, 0.0]
        }
      }
    }
  ]
})");
  return root;
}

bool hasValidationErrors(const demi::ValidationSummary &summary) {
  return std::ranges::any_of(summary.diagnostics, [](const auto &diagnostic) {
    return diagnostic.severity == demi::Severity::Error;
  });
}

} // namespace

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  const bool preserveFixture = argc > 1;
  const fs::path root = createProject(
      preserveFixture
          ? fs::absolute(argv[1]).lexically_normal()
          : fs::temp_directory_path() / "demi_editor_authoring_workflow");

  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root, error));

  // Inspector edit followed by toolbar Undo and Redo.
  assert(workspace.editValue({.entityId = "root", .field = "name"},
                             "Edited Root", false, error));
  assert(workspace.undo(error));
  assert(workspace.selectedEntity() != nullptr);
  assert(workspace.selectedEntity()->name == "Root");
  assert(workspace.redo(error));
  assert(workspace.selectedEntity()->name == "Edited Root");

  // Hierarchy context-menu duplication and drag/drop reparenting.
  assert(workspace.duplicateEntity("root", error));
  assert(workspace.sceneDocument().entity("root_copy") != nullptr);
  assert(workspace.sceneDocument().entity("child_copy") != nullptr);
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().entity("root_copy") == nullptr);
  assert(workspace.redo(error));
  assert(workspace.reparentEntity("child_copy", std::string("root"), error));
  assert(workspace.undo(error));
  assert(workspace.sceneDocument()
             .component("child_copy", "Transform3D")
             ->at("parent") == "root_copy");
  assert(workspace.redo(error));

  // Inspector component add/remove actions use the same reversible path.
  assert(workspace.addComponent("child_copy", "AudioListener", error));
  assert(workspace.sceneDocument().component("child_copy", "AudioListener") !=
         nullptr);
  assert(workspace.removeComponent("child_copy", "AudioListener", error));
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().component("child_copy", "AudioListener") !=
         nullptr);
  assert(workspace.redo(error));
  assert(workspace.sceneDocument().component("child_copy", "AudioListener") ==
         nullptr);

  // Hierarchy deletion is atomic and restores the full subtree on Undo.
  assert(workspace.deleteEntity("root_copy", error));
  assert(workspace.sceneDocument().entity("root_copy") == nullptr);
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().entity("root_copy") != nullptr);

  // Save through the workspace, validate through the CLI's shared service, and
  // reopen through the runtime project/scene loaders.
  assert(workspace.save(error));
  assert(!workspace.sceneDocument().isDirty());
  assert(!hasValidationErrors(demi::validatePath(root)));

  demi::editor::EditorWorkspace reopened;
  assert(reopened.open(root, error));
  assert(reopened.sceneDocument().entity("root_copy") != nullptr);
  assert(reopened.sceneDocument().entity("root")->at("name") == "Edited Root");

  // Reloading a conflicting but unloadable external scene is atomic across
  // the authored document and preview world.
  assert(reopened.editValue({.entityId = "root", .field = "name"},
                            "Keep In Memory", false, error));
  const std::string beforeFailedReload = reopened.sceneDocument().json().dump();
  write(root / "scenes/main.scene.json", R"({
  "format_version": 1,
  "id": "scene://editor_workflow/main",
  "name": "Broken External Scene",
  "entities": [{
    "id": "orphan",
    "components": {
      "Transform3D": { "parent": "missing" }
    }
  }]
})");
  assert(!reopened.save(error));
  assert(reopened.sceneDocument().hasExternalConflict());
  assert(!reopened.resolveExternalChange(
      demi::editor::ExternalChangeDecision::ReloadFromDisk, {}, error));
  assert(reopened.sceneDocument().json().dump() == beforeFailedReload);
  assert(reopened.sceneDocument().hasExternalConflict());

  if (!preserveFixture) {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  return 0;
}
