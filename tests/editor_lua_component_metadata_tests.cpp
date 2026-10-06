#include "editor/EditorLuaComponentMetadata.h"
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

  fs::remove_all(root, ignored);
}
