#include "demi/schema/Validation.h"
#include "editor/EditorCodeEditor.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorSourceCreation.h"
#include "editor/EditorSpecializedDocument.h"
#include "editor/EditorWorkspace.h"
#include <chrono>
#include <fstream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  const auto root =
      fs::temp_directory_path() /
      ("demi-source-workflow-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root / "scenes");
  {
    std::ofstream p(root / "demi.project.json");
    p << R"({"format_version":1,"name":"Authoring test","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})";
  }
  {
    std::ofstream p(root / "scenes/main.scene.json");
    p << R"({"format_version":1,"id":"scene://main","entities":[]})";
  }
  EditorWorkspace workspace;
  std::string error;
  fs::path created;
  assert(workspace.open(root, error));
  assert(createEditorSource(workspace, EditorSourceKind::Scene3D,
                            "levels/first", created, error));
  const auto scene = created;
  assert(workspace.openSceneDocument(scene, error));
  assert(workspace.sceneDocument().json()["id"] == "scene://levels/first");
  const auto unchangedScene = workspace.sceneDocument().json();
  assert(createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                            "camera_copy", created, error, "camera",
                            "prefabs/props"));
  assert(created == root / "prefabs/props/camera_copy.prefab.json");
  {
    std::ifstream source(created);
    const auto prefab = nlohmann::json::parse(source);
    assert(prefab["id"] == "prefab://props/camera_copy");
    assert(prefab["entities"][0]["id"] == "camera");
  }
  assert(workspace.sceneDocument().json() == unchangedScene);
  assert(!workspace.sceneDocument().canUndo());
  assert(!createEditorSource(workspace,
                             EditorSourceKind::PrefabFromSelection,
                             "camera_copy", created, error, "camera",
                             "prefabs/props"));
  assert(error.find("already exists") != std::string::npos);
  assert(!createEditorSource(workspace,
                             EditorSourceKind::PrefabFromSelection,
                             "outside", created, error, "camera", "scenes"));
  assert(workspace.sceneDocument().json() == unchangedScene);
  assert(!workspace.sceneDocument().canUndo());
  assert(!createEditorSource(workspace, EditorSourceKind::Scene3D,
                             "levels/first", created, error));
  assert(!createEditorSource(workspace, EditorSourceKind::Lua, "../escape",
                             created, error));
  assert(createEditorSource(workspace, EditorSourceKind::Hud, "overlay",
                            created, error));
  const auto hud = created;
  assert(workspace.setSceneHud(hud, error));
  assert(workspace.hudDocument() && workspace.hudDocument()->path() == hud);
  assert(workspace.undo(error));
  assert(!workspace.sceneDocument().json().contains("hud"));
  assert(workspace.redo(error));
  assert(workspace.save(error));
  for (const auto &[kind, name] :
       std::vector<std::pair<EditorSourceKind, const char *>>{
           {EditorSourceKind::Scene2D, "second"},
           {EditorSourceKind::Prefab, "prop"},
           {EditorSourceKind::UiPrefab, "button"},
           {EditorSourceKind::Lua, "behaviour"}})
    assert(createEditorSource(workspace, kind, name, created, error));
  assert(!demi::hasErrors(demi::validatePath(root).diagnostics));
  for (auto kind : {EditorSourceKind::Material, EditorSourceKind::Data}) {
    assert(
        createEditorSource(workspace, kind, "test/new_asset", created, error));
    EditorSpecializedDocument document;
    assert(document.open(created, workspace.assetIndex(), error));
    assert(
        !createEditorSource(workspace, kind, "test/new_asset", created, error));
  }
  assert(!demi::hasErrors(demi::validatePath(root).diagnostics));
  assert(prepareCodeEditorWorkspace(
      root, fs::path(DEMI_SOURCE_DIR) / "scripts/stubs", error));
  assert(fs::is_regular_file(root / ".demi/lua/demi/input.lua"));
  {
    std::ofstream p(root / ".luarc.json");
    p << "{\"custom\":true}";
  }
  assert(prepareCodeEditorWorkspace(
      root, fs::path(DEMI_SOURCE_DIR) / "scripts/stubs", error));
  {
    std::ifstream p(root / ".luarc.json");
    assert(nlohmann::json::parse(p).contains("custom"));
  }
  EditorPreferences preferences;
  const auto command =
      codeEditorCommand(preferences, root, root / "scripts/a ; b.lua");
  assert(command.size() == 5 && command[0] == "code" &&
         command[4].ends_with("a ; b.lua"));
  EditorPlaySession play;
  assert(play.startEmbedded(root / "demi.project.json", error,
                            "scene://levels/first"));
  assert(play.runtimeWorld()->activeSceneId == "scene://levels/first");
  play.stop();
  fs::remove_all(root);
}
