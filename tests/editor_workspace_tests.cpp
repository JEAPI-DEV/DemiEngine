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
  return 0;
}
