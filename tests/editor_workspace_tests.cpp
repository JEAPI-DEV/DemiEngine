#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>

int main() {
  const std::filesystem::path root = DEMI_SOURCE_DIR;
  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root / "examples/minimal_voxel", error));
  assert(error.empty());
  assert(workspace.project().project.name == "Minimal Voxel");
  assert(!workspace.project().world.entities.empty());
  assert(workspace.selectedEntity() != nullptr);

  const bool foundLuaSource = std::ranges::any_of(
      workspace.sources(), [](const std::filesystem::path &path) {
        return path.filename() == "worldgen.lua";
      });
  assert(foundLuaSource);

  workspace.selectEntity("missing-entity");
  assert(workspace.selectedEntity() == nullptr);
  workspace.selectEntity(workspace.project().world.entities.front().id);
  assert(workspace.selectedEntity() != nullptr);

  const std::string entityId = workspace.selectedEntity()->id;
  const nlohmann::json *authored = workspace.sceneDocument().entity(entityId);
  assert(authored != nullptr);
  if (authored->contains("name")) {
    const std::string originalName = workspace.selectedEntity()->name;
    assert(workspace.editValue({.entityId = entityId, .field = "name"},
                               "Edited in memory", false, error));
    assert(workspace.selectedEntity()->name == "Edited in memory");
    assert(workspace.sceneDocument().isDirty());
    assert(workspace.undo(error));
    assert(workspace.selectedEntity()->name == originalName);
    assert(!workspace.sceneDocument().isDirty());
  }

  const std::size_t entityCount = workspace.project().world.entities.size();
  assert(workspace.createEntity(error));
  assert(workspace.project().world.entities.size() == entityCount + 1);
  assert(workspace.selectedEntity() != nullptr);
  assert(workspace.selectedEntity()->id ==
         workspace.sceneDocument().lastChangedEntityId());
  const std::string createdId(workspace.selectedEntityId());
  assert(workspace.undo(error));
  assert(workspace.project().world.entities.size() == entityCount);
  assert(workspace.sceneDocument().entity(createdId) == nullptr);

  const std::string selectedId(workspace.selectedEntityId());
  const std::string beforeInvalidEdit = workspace.sceneDocument().json().dump();
  assert(!workspace.editValue({.entityId = selectedId, .field = "name"}, 42,
                              false, error));
  assert(workspace.sceneDocument().json().dump() == beforeInvalidEdit);
  assert(workspace.sceneDocument().issueFor(
             {.entityId = selectedId, .field = "name"}) != nullptr);
  assert(std::ranges::any_of(workspace.diagnostics(), [](const auto &diagnostic) {
    return diagnostic.code == "EDITOR_SCENE_EDIT_REJECTED";
  }));

  // A preview rebuild failure restores the complete document and its history.
  const std::string documentBeforeFailure = workspace.sceneDocument().json().dump();
  const bool couldUndoBeforeFailure = workspace.sceneDocument().canUndo();
  const bool couldRedoBeforeFailure = workspace.sceneDocument().canRedo();
  workspace.project().project.mainScene = "scene://missing";
  assert(!workspace.createEntity(error));
  assert(error.find("No scene registered") != std::string::npos);
  assert(workspace.sceneDocument().json().dump() == documentBeforeFailure);
  assert(workspace.sceneDocument().canUndo() == couldUndoBeforeFailure);
  assert(workspace.sceneDocument().canRedo() == couldRedoBeforeFailure);
  assert(std::ranges::any_of(workspace.diagnostics(), [](const auto &diagnostic) {
    return diagnostic.code == "EDITOR_PREVIEW_REBUILD_FAILED";
  }));
  return 0;
}
