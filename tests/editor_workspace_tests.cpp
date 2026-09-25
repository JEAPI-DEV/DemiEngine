#include "editor/EditorWorkspace.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/PrefabPlacements3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  assert(
      std::ranges::any_of(workspace.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code == "EDITOR_SCENE_EDIT_REJECTED";
      }));

  // Expanded prefab children edit the owning scene instance override while
  // retaining their expanded id for selection and live preview updates.
  demi::editor::EditorWorkspace prefabWorkspace;
  error.clear();
  assert(prefabWorkspace.open(root / "examples/minimal_3d", error));
  prefabWorkspace.selectEntity("player/body");
  assert(prefabWorkspace.selectedEntity() != nullptr);
  assert(prefabWorkspace.selectedEntity()->prefabInstance == "player");
  const nlohmann::json originalPosition =
      nlohmann::json::parse(
          prefabWorkspace.selectedEntity()->serializedComponents.at(
              "Transform3D"))
          .at("position");
  const demi::editor::SceneValueTarget prefabPosition{.entityId = "player/body",
                                                      .component =
                                                          "Transform3D",
                                                      .field = "position"};
  const std::string prefabDocumentBeforeEdit =
      prefabWorkspace.sceneDocument().json().dump();
  for (int frame = 0; frame < 60; ++frame) {
    assert(prefabWorkspace.editValue(
        prefabPosition, {3.0 + frame * 0.01, 2.0, 1.0}, true, error));
  }
  prefabWorkspace.endContinuousEdit();
  assert(prefabWorkspace.hasExplicitValue(prefabPosition));
  const demi::editor::SceneValueTarget authoredPrefabPosition =
      prefabWorkspace.authoredTarget(prefabPosition);
  assert(*demi::editor::valueInDocument(prefabWorkspace.sceneDocument().json(),
                                        authoredPrefabPosition) ==
         nlohmann::json({3.59, 2.0, 1.0}));
  assert(nlohmann::json::parse(
             prefabWorkspace.selectedEntity()->serializedComponents.at(
                 "Transform3D"))
             .at("position") == nlohmann::json({3.59, 2.0, 1.0}));
  assert(prefabWorkspace.undo(error));
  assert(prefabWorkspace.sceneDocument().json().dump() ==
         prefabDocumentBeforeEdit);
  assert(nlohmann::json::parse(
             prefabWorkspace.selectedEntity()->serializedComponents.at(
                 "Transform3D"))
             .at("position") == originalPosition);

  // Scene assets replace the authored scene document, while independently
  // opened HUDs retain their own tab and do not replace the scene's HUD.
  demi::editor::EditorWorkspace androidWorkspace;
  error.clear();
  if (!androidWorkspace.open(root / "examples/minimal_2d_android", error)) {
    std::cerr << "Could not open Android editor fixture: " << error << '\n';
    return 1;
  }
  assert(androidWorkspace.openSceneDocument(
      root / "examples/minimal_2d_android/scenes/spiral.scene.json", error));
  assert(androidWorkspace.project().world.activeSceneId ==
         "scene://minimal_2d_android/spiral");
  assert(androidWorkspace.sceneDocument().path().filename() ==
         "spiral.scene.json");
  assert(androidWorkspace.createEntity(error));
  assert(androidWorkspace.project().world.activeSceneId ==
         "scene://minimal_2d_android/spiral");
  assert(!androidWorkspace.openSceneDocument(
      root / "examples/minimal_2d_android/scenes/menu.scene.json", error));
  assert(error.find("Save or undo") != std::string::npos);
  error.clear();
  assert(androidWorkspace.undo(error));

  const std::size_t sceneHudNodes =
      androidWorkspace.project().world.ui.nodes.size();
  assert(androidWorkspace.openHudDocument(
      root / "examples/minimal_2d_android/scenes/menu.hud.json", error));
  assert(androidWorkspace.activeDocument() ==
         demi::editor::EditorWorkspaceDocument::Hud);
  assert(androidWorkspace.hudDocument()->path().filename() == "menu.hud.json");
  assert(!androidWorkspace.displayedHud().nodes.empty());
  const std::size_t openedHudNodes =
      androidWorkspace.displayedHud().nodes.size();
  assert(androidWorkspace.createHudNode("label", error));
  assert(androidWorkspace.displayedHud().nodes.size() == openedHudNodes + 1);
  assert(androidWorkspace.project().world.ui.nodes.size() == sceneHudNodes);
  assert(androidWorkspace.undo(error));
  androidWorkspace.activateSceneDocument();
  assert(androidWorkspace.activeDocument() ==
         demi::editor::EditorWorkspaceDocument::Scene);
  assert(androidWorkspace.hudDocument()->path().filename() == "game.hud.json");
  androidWorkspace.activateHudDocument();
  assert(androidWorkspace.hudDocument()->path().filename() == "menu.hud.json");

  // A preview rebuild failure restores the complete document and its history.
  demi::editor::EditorWorkspace barrelWorkspace;
  error.clear();
  assert(barrelWorkspace.open(root / "examples/performance_3d_lab", error));
  const auto barrelSource =
      root / "examples/performance_3d_lab/prefabs/barrel.prefab.json";
  assert(barrelWorkspace.openPrefabDocument(barrelSource, error));
  assert(barrelWorkspace.addComponent("body", "Dentable3D", error));
  assert(barrelWorkspace.sceneDocument().component("body", "Dentable3D")->empty());
  assert(barrelWorkspace.selectedEntity()->hasComponent<demi::runtime::Dentable3DComponent>());
  assert(barrelWorkspace.undo(error));
  assert(!barrelWorkspace.selectedEntity()->hasComponent<demi::runtime::Dentable3DComponent>());
  assert(barrelWorkspace.isPrefabDocument());
  assert(barrelWorkspace.project().world.entities.size() == 1);
  assert(barrelWorkspace.selectedEntityId() == "body");
  assert(barrelWorkspace.sceneDocument().json()["id"] == "prefab://barrel");
  assert(barrelWorkspace.project().project.mainScene ==
         "scene://performance_3d_lab/main");
  const demi::editor::SceneValueTarget barrelName{.entityId = "body",
                                                  .field = "name"};
  assert(barrelWorkspace.editValue(barrelName, "Preview edit", false, error));
  assert(barrelWorkspace.selectedEntity()->name == "Preview edit");
  assert(!barrelWorkspace.openSceneDocument(barrelWorkspace.lastScenePath(),
                                            error));
  assert(barrelWorkspace.dirtyDocuments().front().kind == "prefab");
  assert(barrelWorkspace.undo(error));
  assert(barrelWorkspace.redo(error));
  assert(barrelWorkspace.undo(error));
  assert(barrelWorkspace.createEntity(error));
  assert(barrelWorkspace.project().world.entities.size() == 2);
  assert(barrelWorkspace.undo(error));
  assert(barrelWorkspace.refresh(error));
  assert(barrelWorkspace.isPrefabDocument());
  assert(barrelWorkspace.project().world.entities.size() == 1);
  assert(barrelWorkspace.openSceneDocument(barrelWorkspace.lastScenePath(),
                                           error));
  assert(!barrelWorkspace.isPrefabDocument());

  // Save only a temporary source; preview fields must never leak into it.
  const auto temporaryPrefab = std::filesystem::temp_directory_path() /
                               "demi-editor-workspace-save.prefab.json";
  {
    std::ofstream output(temporaryPrefab);
    output
        << R"({"format_version":1,"id":"prefab://test","entities":[{"id":"body","components":{"Transform3D":{}}}]})";
  }
  assert(barrelWorkspace.openPrefabDocument(temporaryPrefab, error));
  assert(barrelWorkspace.addComponent("body", "MeshRenderer", error));
  assert(barrelWorkspace.addComponent("body", "Dentable3D", error));
  assert(barrelWorkspace.editValue(barrelName, "Saved name", false, error));
  assert(barrelWorkspace.save(error));
  assert(barrelWorkspace.sceneDocument().reload(error));
  assert(barrelWorkspace.sceneDocument().json()["id"] == "prefab://test");
  assert(barrelWorkspace.sceneDocument().entity("body")->at("name") ==
         "Saved name");
  assert(!barrelWorkspace.sceneDocument().json().contains("hud"));
  assert(barrelWorkspace.sceneDocument().component("body", "Dentable3D") != nullptr);
  std::filesystem::remove(temporaryPrefab);

  {
    using namespace demi::runtime;
    demi::editor::EditorWorkspace quality;
    assert(quality.open(root / "examples/shadows_3d",error));
    const auto original=quality.sceneDocument().json();
    assert(quality.editValue({.entityId="environment",.component="Environment3D",.field="msaa_samples"},8,false,error));
    assert(findEntity(quality.project().world,"environment")->component<Environment3DComponent>()->msaaSamples==8);
    assert(!quality.editValue({.entityId="environment",.component="Environment3D",.field="msaa_samples"},3,false,error));
    assert(quality.undo(error));
    assert(quality.sceneDocument().json()==original);
    assert(findEntity(quality.project().world,"environment")->component<Environment3DComponent>()->msaaSamples==4);
  }
  {
    using namespace demi::runtime;
    demi::editor::EditorWorkspace placements;
    assert(placements.open(root / "examples/destruction_weapons_3d_lab", error));
    const auto authored = placements.sceneDocument().json().dump();
    const auto *preview = findEntity(placements.project().world, "a/__preview/assembly");
    assert(preview);
    std::size_t previewCells = 0;
    for (const auto &entity : placements.project().world.entities) {
      if (entity.id.find("/__masonry_preview/") == std::string::npos) continue;
      ++previewCells;
      assert(entity.hasComponent<MeshRendererComponent>());
      assert(!entity.serializedComponents.contains("Fracture3D"));
      assert(!entity.serializedComponents.contains("Rigidbody3D"));
      assert(!placements.sceneDocument().entity(entity.id));
    }
    assert(previewCells > 0);
    assert(!placements.sceneDocument().entity("a/__preview/assembly"));
    auto entries = collectPrefabPlacements3D(placements.project().world, "world_stream");
    assert(entries.size()==2 && entries.front().id=="a");
    assert(entries.front().transform.position.x==-2.8F);
    placements.selectEntity("a/__preview/assembly");
    assert(placements.selectedEntityId()=="a");
    assert(placements.editValue({.entityId="a",.component="Transform3D",.field="position"},
                                nlohmann::json::array({-4,0,0}),false,error));
    preview = findEntity(placements.project().world, "a/__preview/assembly");
    assert(resolveWorldTransform3D(placements.project().world,*preview)->position.x==-4);
    assert(placements.undo(error));
    assert(placements.sceneDocument().json().dump()==authored);
    assert(placements.editValue({.entityId="world_stream",.component="Transform3D",.field="rotation"},
                                nlohmann::json::array({0,1,0}),false,error));
    entries=collectPrefabPlacements3D(placements.project().world,"world_stream");
    preview=findEntity(placements.project().world,"a/__preview/assembly");
    const auto transformed=resolveWorldTransform3D(placements.project().world,*preview);
    assert(std::abs(transformed->position.z-entries.front().transform.position.z)<.0001F);
    assert(std::abs(transformed->rotation.y-entries.front().transform.rotation.y)<.0001F);
    assert(placements.undo(error));
    assert(!placements.editValue({.entityId="a",.component="PrefabPlacement3D",.field="prefab"},
                                 "prefab://does_not_exist",false,error));
    assert(placements.sceneDocument().json().dump()==authored);
    assert(placements.editValue({.entityId="a",.field="enabled"},false,false,error));
    assert(collectPrefabPlacements3D(placements.project().world,"world_stream").size()==1);
    assert(placements.undo(error));
    assert(!placements.editValue({.entityId="a",.component="Transform3D",.field="scale"},
                                 nlohmann::json::array({0,1,1}),false,error));
    assert(placements.sceneDocument().json().dump()==authored);
    assert(placements.duplicateEntity("a",error));
    const auto duplicate=std::string(placements.sceneDocument().lastChangedEntityId());
    assert(collectPrefabPlacements3D(placements.project().world,"world_stream").size()==3);
    assert(findEntity(placements.project().world,duplicate+"/__preview/assembly"));
    assert(placements.undo(error));
    assert(placements.sceneDocument().json().dump()==authored);
    auto runtime=loadProject(root / "examples/destruction_weapons_3d_lab/demi.project.json",error);
    assert(runtime && findEntity(runtime->world,"a"));
    assert(!findEntity(runtime->world,"a/__preview/assembly"));
    assert(!findEntity(runtime->world,"a/assembly")); // Only gameplay activates it.
    assert(placements.editValue({.entityId="world_stream",.field="enabled"},false,false,error));
    assert(collectPrefabPlacements3D(placements.project().world,"world_stream").empty());
    assert(!findEntity(placements.project().world,"a/__preview/assembly")->enabled);
    assert(placements.undo(error));
    const auto *floor=placements.sceneDocument().component("floor","MeshRenderer");
    const auto revision=findEntity(placements.project().world,"floor")->component<MeshRendererComponent>()->revision;
    auto uvs=(*floor)["uvs"];
    uvs[0][0]=5;
    assert(placements.editValue({.entityId="floor",.component="MeshRenderer",.field="uvs"},uvs,false,error));
    assert((*placements.sceneDocument().component("floor","MeshRenderer"))["uvs"][0][0]==5);
    assert(findEntity(placements.project().world,"floor")->component<MeshRendererComponent>()->revision!=revision);
    assert(placements.undo(error));
    assert(findEntity(placements.project().world,"floor")->component<MeshRendererComponent>()->revision==revision);
    assert(placements.sceneDocument().json().dump()==authored);
  }

  const std::string documentBeforeFailure =
      workspace.sceneDocument().json().dump();
  const bool couldUndoBeforeFailure = workspace.sceneDocument().canUndo();
  const bool couldRedoBeforeFailure = workspace.sceneDocument().canRedo();
  workspace.project().project.scenes.clear();
  assert(!workspace.createEntity(error));
  assert(error.find("no longer registered") != std::string::npos);
  assert(workspace.sceneDocument().json().dump() == documentBeforeFailure);
  assert(workspace.sceneDocument().canUndo() == couldUndoBeforeFailure);
  assert(workspace.sceneDocument().canRedo() == couldRedoBeforeFailure);
  assert(
      std::ranges::any_of(workspace.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code == "EDITOR_PREVIEW_REBUILD_FAILED";
      }));
  return 0;
}
