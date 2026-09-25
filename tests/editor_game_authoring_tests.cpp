#include "cli/build/BuildService.h"
#include "cli/project/ProjectTemplates.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/schema/Validation.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorSourceCreation.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  try {
    require(argc == 2, "Expected runtime executable argument");
    const auto root =
        fs::temp_directory_path() / "demi_editor_game_authoring_test";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    demi::Diagnostics diagnostics;
    demi::cli::project::ProjectTemplateCatalog catalog(
        fs::path(DEMI_SOURCE_DIR) / "templates");
    const auto starter = catalog.find("blank-2d", diagnostics);
    require(starter.has_value(), "Blank 2D template unavailable");
    const auto scaffold = demi::cli::project::ProjectScaffolder{}.create(
        {.projectTemplate = *starter,
         .destination = root,
         .projectName = "Editor Authored Game"});
    require(scaffold.committed, "Editor new-project service failed");

    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(root, error), error);
    fs::path scene;
    require(createEditorSource(workspace, EditorSourceKind::Scene2D, "arena",
                               scene, error),
            error);
    require(workspace.openSceneDocument(scene, error), error);
    require(workspace.createPresetEntity("prop_2d", error), error);
    const std::string actor(workspace.selectedEntityId());
    require(workspace.createPresetEntity("prop_2d", error), error);
    const std::string group(workspace.selectedEntityId());
    require(workspace.reparentEntity(actor, group, error), error);
    require(workspace.undo(error), error);
    require(workspace.deleteEntity(group, error), error);
    workspace.selectEntity(actor);
    const auto beforePresetEdit = workspace.sceneDocument().json();
    require(workspace.editValue({.entityId = actor, .component = "Transform2D", .field = "position"},
                {20, 30}, true, error), error);
    require(workspace.editValue({.entityId = actor, .component = "Transform2D", .field = "position"},
                {40, 30}, true, error), error);
    workspace.endContinuousEdit();
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == beforePresetEdit,
            "Undo did not restore compact preset authoring");
    require(workspace.redo(error), error);
    require(workspace.sceneDocument().entity(actor)->contains("preset"),
            "Editing a preset field unpacked it implicitly");
    const auto compactPreset = workspace.sceneDocument().json();
    require(workspace.unpackPreset(actor, error), error);
    require(!workspace.sceneDocument().entity(actor)->contains("preset"), "Unpack kept the preset");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == compactPreset, "Unpack undo lost source layout");
    require(workspace.editValue({.entityId = actor, .field = "name"}, "Player",
                                false, error),
            error);
    fs::path script;
    require(createEditorSource(workspace, EditorSourceKind::Lua, "player",
                               script, error),
            error);
    // The only hand-authored file is gameplay code, as it would be in an
    // external editor.
    {
      std::ofstream code(script);
      code << R"lua(---@demi_component
local Input = require("demi.input")
local Transform = require("demi.transform2d")
local Player = {}
function Player:on_update(dt)
  Transform.add_position(self.entity_id, Input.value("move_x") * dt * 100, 0)
end
return Player
)lua";
    }
    const auto scripts = discoverEditorLuaComponents(root, workspace.sources());
    const auto behaviour = std::ranges::find(scripts.components,
        "script://scripts/player.lua", &EditorLuaComponentMetadata::module);
    require(behaviour != scripts.components.end(), "Player behaviour did not appear in Add Component");
    require(workspace.addScriptComponent(actor, *behaviour, error), error);
    fs::path prefab;
    require(createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                               "actors/player", prefab, error, actor),
            error);
    require(workspace.sceneDocument().entity(actor) != nullptr,
            "Export replaced the source actor");
    const auto beforePrefabDrop = workspace.sceneDocument().json();
    require(workspace.instantiatePrefab(
                prefab, demi::runtime::Vec2{60.0F, 20.0F}, error),
            error);
    const std::string instanceActor(workspace.selectedEntityId());
    require(instanceActor != actor && !instanceActor.empty(),
            "Prefab placement was not selected");
    const auto *placedActor = demi::runtime::findEntity(
        workspace.project().world, instanceActor);
    require(placedActor != nullptr &&
                placedActor->component<
                    demi::runtime::Transform2DComponent>() != nullptr &&
                placedActor->component<demi::runtime::Transform2DComponent>()
                        ->position.x == 60.0F &&
                placedActor->component<demi::runtime::Transform2DComponent>()
                        ->position.y == 20.0F,
            "2D prefab drop did not use the cursor world position");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == beforePrefabDrop,
            "Prefab drop Undo did not remove insertion and placement together");
    require(workspace.redo(error), error);
    require(workspace.editValue({.entityId = instanceActor,
                                 .component = "Transform2D",
                                 .field = "position"},
                                {65, 25}, false, error),
            error);
    require(workspace.undo(error) && workspace.redo(error), error);
    require(workspace.duplicatePrefabInstance(instanceActor, error), error);
    const std::string duplicate(workspace.selectedEntityId());
    require(workspace.removePrefabInstance(duplicate, error), error);
    require(workspace.undo(error) && workspace.redo(error), error);
    require(workspace.saveAll(error), error);

    fs::path uiPrefab;
    require(createEditorSource(workspace, EditorSourceKind::UiPrefab,
                               "widgets/start", uiPrefab, error),
            error);
    require(workspace.openHudDocument(uiPrefab, error), error);
    require(workspace.createHudNode("button", error), error);
    const std::string button(workspace.selectedHudNodeId());
    require(workspace.setHudNodeField(button, "text", "Start game", error),
            error);
    require(workspace.setHudNodeField(button, "size", {220, 56}, error), error);
    workspace.selectHudNode("root");
    require(workspace.createHudNode("panel", error), error);
    const std::string panel(workspace.selectedHudNodeId());
    require(workspace.reparentHudNode(button, panel, error), error);
    require(workspace.duplicateHudNode(panel, error), error);
    require(workspace.undo(error) && workspace.redo(error), error);
    require(workspace.saveHud(error), error);
    fs::path hud;
    require(createEditorSource(workspace, EditorSourceKind::Hud, "game", hud,
                               error),
            error);
    require(workspace.openHudDocument(hud, error), error);
    require(workspace.setHudCanvasSize({1280, 720}, error), error);
    require(
        workspace.createHudPrefabInstance("ui-prefab://widgets/start", error),
        error);
    require(workspace.undo(error) && workspace.redo(error), error);
    require(workspace.saveHud(error), error);
    require(workspace.openSceneDocument(scene, error), error);
    require(workspace.setSceneHud(hud, error), error);
    require(workspace.setProjectMainScene("scene://arena", error), error);
    require(workspace.saveAll(error), error);

    EditorWorkspace reopened;
    require(reopened.open(root, error), error);
    require(reopened.sceneDocument().json().at("id") == "scene://arena",
            "Startup scene was not persisted");
    require(!demi::hasErrors(demi::validatePath(root).diagnostics),
            "Editor-authored game does not validate");
    const auto *authored =
        demi::runtime::findEntity(reopened.project().world, actor);
    require(authored != nullptr, "Authored player was lost on reopen");
    const float originalX =
        authored->component<demi::runtime::Transform2DComponent>()->position.x;
    EditorPlaySession play;
    require(play.startEmbedded(root / "demi.project.json", error), error);
    demi::runtime::InputState input;
    input.keysDown.insert("d");
    for (int frame = 0; frame < 5; ++frame)
      require(play.update(input, 1.0F / 60.0F, 1280, 720, error), error);
    const auto *player = demi::runtime::findEntity(*play.runtimeWorld(), actor);
    require(player && player->component<demi::runtime::Transform2DComponent>()
                              ->position.x > originalX,
            "Editor-authored player did not respond to gameplay input");
    play.stop();
    require(authored->component<demi::runtime::Transform2DComponent>()
                    ->position.x == originalX,
            "Play mutated authored state");
    const auto packaged = demi::build::runProjectOperation(
        {.operation = demi::build::ProjectOperation::PackageLinux,
         .projectFile = root / "demi.project.json",
         .outputDirectory = root / "build/shipping",
         .engineRoot = DEMI_SOURCE_DIR,
         .runtimeExecutable = fs::absolute(argv[1])});
    require(packaged.succeeded(),
            "Editor build service could not package the authored game");
    fs::remove_all(root, ignored);
    std::cout << "Editor-only scene, prefab, HUD, gameplay and Linux build "
                 "workflow passed\n";
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
