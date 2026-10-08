#include "editor/EditorDocumentSessions.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorSourceCreation.h"
#include "editor/EditorSourceIndex.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

void checkLiveDiscovery(const std::filesystem::path &root) {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  const std::string component =
      "---@demi_component\nlocal Camera = {}\nreturn Camera\n";
  write(root / "scripts/initial.lua", component);
  EditorSourceIndex index;
  index.rescan(root);
  assert(index.scripts().components.size() == 1);
  const auto initialParses = index.parseCount();
  const auto initialRevision = index.revision();
  for (int frame = 0; frame < 100; ++frame)
    index.poll();
  assert(index.parseCount() == initialParses &&
         index.revision() == initialRevision);
  auto now = EditorSourceIndex::Clock::now();
  auto flush = [&] {
    now += std::chrono::seconds(1);
    index.poll(now);
    index.poll(now + std::chrono::milliseconds(200));
  };
  write(root / "scripts/new.lua", component);
  flush();
  assert(index.scripts().components.size() == 2);
  assert(index.parseCount() == initialParses + 1);
  // Duplicate notifications or saves with identical contents do not reparse.
  write(root / "scripts/new.lua", component);
  flush();
  assert(index.parseCount() == initialParses + 1);
  write(root / "scripts/.save.tmp", component + "-- changed\n");
  fs::rename(root / "scripts/.save.tmp", root / "scripts/new.lua");
  flush();
  assert(index.parseCount() == initialParses + 2);
  fs::rename(root / "scripts/new.lua", root / "scripts/renamed.lua");
  flush();
  assert(index.scripts().components.size() == 2);
  assert(std::ranges::find(index.sources(), root / "scripts/new.lua") ==
         index.sources().end());
  fs::remove(root / "scripts/renamed.lua");
  flush();
  assert(index.scripts().components.size() == 1);
  write(root / "scripts/nested/deeper.lua", component);
  flush();
  assert(index.scripts().components.size() == 2);
  fs::rename(root / "scripts/nested", root / "scripts/moved");
  flush();
  assert(
      std::ranges::find(index.sources(), root / "scripts/moved/deeper.lua") !=
      index.sources().end());
  const auto beforeIgnored = index.parseCount();
  write(root / "generated/ignored.lua", component);
  write(root / "scripts/readme.txt", "unrelated");
  flush();
  assert(index.parseCount() == beforeIgnored);
  write(root / "scripts/initial.lua",
        "---@demi_component\nlocal Broken = "
        "{}\n---@demi_property\nBroken.speed = nope\nreturn Broken\n");
  flush();
  assert(!index.scripts().diagnostics.empty());
  write(root / "scripts/initial.lua", component);
  flush();
  assert(index.scripts().diagnostics.empty());
  // Explicit rescan follows the same cache path used after notification
  // overflow.
  index.rescan(root);
  assert(index.scripts().components.size() == 2);

  write(
      root / "demi.project.json",
      R"({"format_version":1,"name":"Shared","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})");
  write(
      root / "scenes/main.scene.json",
      R"({"format_version":1,"id":"scene://main","entities":[{"id":"camera","components":{"Transform3D":{}}}]})");
  write(
      root / "hud/main.hud.json",
      R"({"format_version":1,"children":[{"id":"label","type":"label","text":"Original"}]})");
  EditorWorkspace scene;
  std::string error;
  assert(scene.open(root, error));
  EditorDocumentSessions sessions(scene);
  assert(sessions.openHud(root / "hud/main.hud.json", error));
  auto &hud = sessions.focused();
  assert(hud.setHudNodeField("label", "text", "Unsaved", error));
  const auto dirtyHud = hud.hudDocument()->json();
  const auto sceneSource = scene.sceneDocument().json();
  fs::path created;
  assert(createEditorSource(hud, EditorSourceKind::Lua, "colony_camera",
                            created, error));
  assert(&scene.scriptCatalog() == &hud.scriptCatalog());
  assert(std::ranges::find(scene.sources(), created) != scene.sources().end());
  assert(std::ranges::find(scene.scriptCatalog().components, created,
                           &EditorLuaComponentMetadata::sourcePath) !=
         scene.scriptCatalog().components.end());
  assert(hud.hudDocument()->json() == dirtyHud && hud.hudDocument()->isDirty());
  assert(scene.sceneDocument().json() == sceneSource &&
         !scene.sceneDocument().canUndo());
  assert(hud.undo(error));
  assert(hud.hudDocument()->authoredNode("label")->at("text") == "Original");
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_lua_component_metadata";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  const fs::path script = root / "scripts/mover.lua";
  write(script, R"(---@demi_component
---@display_name Mover
---@category Gameplay
---@description Moves one entity.
local Mover = {}
---@demi_property
---@range 0 20
Mover.speed = 4.0
---@demi_property boolean
Mover.enabled = true
---@demi_property entity
Mover.target = ""
return Mover)");
  demi::Diagnostic diagnostic;
  const auto metadata =
      parseEditorLuaComponentMetadata(script, root, &diagnostic);
  assert(metadata && diagnostic.code.empty());
  assert(metadata->id == "script-component://scripts/mover.lua");
  assert(metadata->displayName == "Mover");
  assert(metadata->module == "script://scripts/mover.lua");
  assert(metadata->defaultProperties ==
         nlohmann::json({{"speed", 4.0}, {"enabled", true}, {"target", ""}}));
  assert(metadata->propertySchema["target"]["type"] == "entity");
  const auto catalog =
      discoverEditorLuaComponents(root, std::span<const fs::path>(&script, 1));
  assert(catalog.components.size() == 1 && catalog.diagnostics.empty());

  write(root / "demi.project.json", R"({
    "format_version":1,"name":"References","main_scene":"scene://main",
    "scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
  })");
  write(root / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://main","entities":[
      {"id":"actor","components":{"Transform3D":{}}},
      {"id":"target","components":{"Transform3D":{}}}
    ]
  })");
  {
    EditorWorkspace references;
    std::string referenceError;
    assert(references.open(root, referenceError));
    assert(references.addScriptComponent("actor", *metadata, referenceError));
    auto properties = metadata->defaultProperties;
    properties["target"] = "target";
    const SceneValueTarget propertyTarget{
        .entityId = "actor", .component = "LuaScript", .field = "properties"};
    assert(references.editValue(propertyTarget, properties, false, referenceError));
    assert(references.undo(referenceError));
    assert(references.sceneDocument().component("actor", "LuaScript")
               ->at("properties").at("target") == "");
    assert(references.redo(referenceError));
    assert(references.save(referenceError));
    assert(references.open(root, referenceError));
    assert(references.sceneDocument().component("actor", "LuaScript")
               ->at("properties").at("target") == "target");
  }

  const fs::path invalid = root / "scripts/invalid.lua";
  write(invalid, "---@demi_component\nlocal Invalid = {}\n---@demi_property\n"
                 "Invalid.speed = nope\nreturn Invalid\n");
  diagnostic = {};
  assert(!parseEditorLuaComponentMetadata(invalid, root, &diagnostic));
  assert(diagnostic.code == "EDITOR_LUA_COMPONENT_METADATA_INVALID");

  const fs::path source = DEMI_SOURCE_DIR;
  EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(source / "examples/minimal_3d", error));
  const EditorLuaComponentCatalog exampleCatalog = discoverEditorLuaComponents(
      workspace.project().project.projectDirectory, workspace.sources());
  const auto player = std::ranges::find(
      exampleCatalog.components, "script-component://scripts/player_3d.lua",
      &EditorLuaComponentMetadata::id);
  assert(player != exampleCatalog.components.end());
  const auto target = std::ranges::find_if(
      workspace.project().world.entities, [&](const auto &entity) {
        const nlohmann::json *authored =
            workspace.sceneDocument().entity(entity.id);
        return authored != nullptr && workspace.sceneDocument().component(
                                          entity.id, "LuaScript") == nullptr;
      });
  assert(target != workspace.project().world.entities.end());
  const std::string targetId = target->id;
  assert(workspace.addScriptComponent(targetId, *player, error));
  const nlohmann::json *lua =
      workspace.sceneDocument().component(targetId, "LuaScript");
  assert(lua && lua->at("module") == player->module &&
         lua->at("properties") == player->defaultProperties);
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().component(targetId, "LuaScript") == nullptr);
  assert(workspace.redo(error));

  checkLiveDiscovery(root / "live");
  fs::remove_all(root, ignored);
}
