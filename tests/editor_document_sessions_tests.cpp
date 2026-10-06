#include "editor/EditorDocumentSessions.h"
#include "editor/EditorSourceCreation.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/terrain/TerrainRecipe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace demi::editor;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const fs::path &path, const Json &document) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << document.dump(2) << '\n';
  require(output.good(), "Could not write test fixture: " + path.string());
}

Json readJson(const fs::path &path) {
  std::ifstream input(path);
  require(input.good(), "Could not read test fixture: " + path.string());
  return Json::parse(input);
}

struct ProjectFixture {
  fs::path root;
  fs::path terrainSource;
  fs::path prefabSource;
  fs::path hudSource;

  ProjectFixture() {
    const auto pattern =
        (fs::temp_directory_path() / "demi-document-sessions-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *directory = ::mkdtemp(buffer.data());
    require(directory != nullptr, "Could not create document-session fixture");
    root = directory;
    prefabSource = root / "prefabs/prop.prefab.json";
    terrainSource = root / "assets/hill.terrain.json";
    hudSource = root / "scenes/standalone.hud.json";
    writeJson(hudSource, Json::parse(R"({
      "format_version": 1, "canvas_size": [320, 180],
      "root": {"id": "root", "type": "container", "children": [
        {"id": "title", "type": "label", "text": "Independent HUD"}
      ]}
    })"));
    writeJson(root / "demi.project.json", Json::parse(R"({
      "format_version": 1, "name": "Independent document sessions",
      "main_scene": "scene://sessions/main",
      "scenes": [{"id": "scene://sessions/main",
                  "path": "scenes/main.scene.json"}]
    })"));
    writeJson(root / "scenes/main.scene.json", Json::parse(R"({
      "format_version": 1, "id": "scene://sessions/main",
      "hud": "scene_hud.hud.json",
      "entities": [{"id": "main_entity", "components": {"Transform3D": {}}}]
    })"));
    writeJson(root / "scenes/scene_hud.hud.json", Json::parse(R"({
      "format_version": 1, "canvas_size": [320, 180],
      "root": {"id": "root", "type": "container", "children": [
        {"id": "scene_title", "type": "label", "text": "Scene HUD"}
      ]}
    })"));
    writeJson(prefabSource, Json::parse(R"({
      "format_version": 1, "id": "prefab://prop",
      "entities": [{"id": "prop", "components": {"Transform3D": {}}}]
    })"));
    demi::runtime::TerrainRecipe recipe;
    recipe.size = {4, 4};
    recipe.cellsX = recipe.cellsZ = 4;
    recipe.chunkCells = 4;
    writeJson(terrainSource, {{"format_version", 1},
                              {"id", "asset://terrain/hill"},
                              {"recipe", recipe.toJson()}});
    const auto imported =
        demi::assets::importAsset({.projectDirectory = root,
                                   .source = terrainSource,
                                   .id = "asset://terrain/hill",
                                   .type = "Terrain"});
    require(!demi::hasErrors(imported.diagnostics), "Terrain import failed");
    const auto manifest = demi::loadAssetManifest(imported.manifestPath);
    require(manifest.has_value() && manifest->type == "Terrain",
            "The official import did not produce a Terrain manifest");
    terrainSource = manifest->sourcePath;
  }

  ~ProjectFixture() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean fixture: " << error.message() << '\n';
  }
  ProjectFixture(const ProjectFixture &) = delete;
  ProjectFixture &operator=(const ProjectFixture &) = delete;
};

void regenerate(EditorWorkspace &workspace, const int seed) {
  workspace.terrainAuthoring().draft()["seed"] = seed;
  std::string error;
  require(workspace.terrainAuthoring().generate({}, error), error);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (workspace.terrainAuthoring().pendingEdits()) {
    error.clear();
    require(workspace.pollTerrainAuthoring(error), error);
    require(std::chrono::steady_clock::now() < deadline,
            "Terrain generation did not complete in the session test");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void verifyBootstrapAndIndependentHistory() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  EditorWorkspace context;
  require(context.openProjectContext(scene, error), error);
  require(context.project().world.entities.empty() &&
              context.sceneDocument().path().empty(),
          "Project-only bootstrap loaded a main scene projection");

  const auto sceneBefore = scene.sceneDocument().json();
  // The main source is intentionally invalid while opening the other views:
  // their project context must not read and instantiate the main scene.
  writeJson(fixture.root / "scenes/main.scene.json", {{"invalid", true}});
  require(sessions.openPrefab(fixture.prefabSource, error), error);
  require(sessions.openTerrainAsset(fixture.terrainSource, error), error);
  require(sessions.openHud(fixture.hudSource, error), error);
  writeJson(fixture.root / "scenes/main.scene.json", sceneBefore);
  // The deliberate disk rewrite changes the document revision even though its
  // JSON is restored. Acknowledge it while clean, before making authored edits;
  // this reloads only the document baseline, not the existing preview world.
  const auto *sceneStorage = scene.project().world.entities.data();
  require(!scene.sceneDocument().isDirty(),
          "The bootstrap fixture unexpectedly edited the main document");
  require(scene.sceneDocument().reload(error), error);
  require(scene.project().world.entities.data() == sceneStorage,
          "Rebasing the restored test source rebuilt the main scene world");
  const auto *prefab = sessions.workspace(EditorDocumentSession::Prefab);
  const auto *terrain = sessions.workspace(EditorDocumentSession::TerrainAsset);
  const auto *hud = sessions.workspace(EditorDocumentSession::Hud);
  require(
      prefab && terrain &&
          demi::runtime::findEntity(prefab->project().world, "prop") &&
          !demi::runtime::findEntity(prefab->project().world, "main_entity") &&
          !demi::runtime::findEntity(terrain->project().world, "main_entity"),
      "Secondary workspaces contain the main world");
  require(hud && hud->project().world.entities.empty() &&
              hud->sceneDocument().path().empty() &&
              hud->activeDocument() == EditorWorkspaceDocument::Hud &&
              hud->displayedHud().nodes.size() == 2,
          "HUD bootstrap loaded a scene or failed to display its opened HUD");
  require(scene.sceneDocument().json() == sceneBefore,
          "Opening secondary views changed the scene document");
  require(scene.createEntity(error), error);
  auto &prefabWorkspace = *sessions.workspace(EditorDocumentSession::Prefab);
  require(prefabWorkspace.createEntity(error), error);
  auto &terrainWorkspace =
      *sessions.workspace(EditorDocumentSession::TerrainAsset);
  regenerate(terrainWorkspace, 321);
  const auto prefabEdited = prefabWorkspace.sceneDocument().json();
  require(scene.undo(error), error);
  require(prefabWorkspace.sceneDocument().json() == prefabEdited,
          "Scene Undo changed the prefab history");
  require(scene.redo(error), error);
  require(sessions.focus(EditorDocumentSession::Prefab, error), error);
  const auto &readOnly = std::as_const(sessions);
  require(&readOnly.focused() == &prefabWorkspace &&
              readOnly.workspace(EditorDocumentSession::TerrainAsset) ==
                  &terrainWorkspace,
          "Const session accessors do not match the focused workspace");
  require(sessions.saveAll(error), error);
  require(!sessions.hasUnsavedChanges(),
          "Save All left a committed source dirty");
  require(readJson(fixture.terrainSource)["recipe"]["seed"] == 321,
          "Save All did not save the independent terrain source");
}

void verifyRecoveryRoutingAndAtomicFailure() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openPrefab(fixture.prefabSource, error), error);
  require(sessions.openTerrainAsset(fixture.terrainSource, error), error);
  require(sessions.openHud(fixture.hudSource, error), error);
  require(scene.createEntity(error), error);
  require(
      sessions.workspace(EditorDocumentSession::Prefab)->createEntity(error),
      error);
  regenerate(*sessions.workspace(EditorDocumentSession::TerrainAsset), 456);
  require(sessions.workspace(EditorDocumentSession::Hud)
              ->setHudNodeField("title", "text", "Recovered HUD", error),
          error);
  const EditorRecoverySnapshot snapshot{.projectPath = scene.projectPath(),
                                        .documents = sessions.dirtyDocuments()};
  require(snapshot.documents.size() == 4,
          "Recovery did not retain all four dirty document sessions");
  const auto diskScene = readJson(fixture.root / "scenes/main.scene.json");
  const auto diskPrefab = readJson(fixture.prefabSource);
  const auto diskTerrain = readJson(fixture.terrainSource);
  const auto diskHud = readJson(fixture.hudSource);

  EditorWorkspace restoredScene;
  require(restoredScene.open(fixture.root, error), error);
  EditorDocumentSessions restored(restoredScene);
  auto invalid = snapshot;
  for (auto &document : invalid.documents)
    if (document.kind == "prefab-session:prefab")
      document.content["entities"] = "invalid";
  require(!restored.applyRecovery(invalid, error),
          "Invalid prefab recovery was accepted");
  require(restoredScene.sceneDocument().json() == diskScene &&
              restored.workspace(EditorDocumentSession::Prefab) == nullptr &&
              restored.workspace(EditorDocumentSession::TerrainAsset) ==
                  nullptr &&
              restored.workspace(EditorDocumentSession::Hud) == nullptr,
          "Failed recovery partially replaced existing document sessions");
  error.clear();
  require(restored.applyRecovery(snapshot, error), error);
  require(restoredScene.sceneDocument().json() == scene.sceneDocument().json(),
          "Scene recovery was routed to the wrong workspace");
  require(restored.workspace(EditorDocumentSession::Prefab)
                  ->sceneDocument()
                  .json() == sessions.workspace(EditorDocumentSession::Prefab)
                                 ->sceneDocument()
                                 .json(),
          "Prefab recovery was routed to the wrong workspace");
  require(restored.workspace(EditorDocumentSession::TerrainAsset)
                  ->terrainAssetDocument()
                  ->json() ==
              sessions.workspace(EditorDocumentSession::TerrainAsset)
                  ->terrainAssetDocument()
                  ->json(),
          "Terrain recovery was routed to the wrong workspace");
  require(
      restored.workspace(EditorDocumentSession::Hud)->hudDocument()->json() ==
              sessions.workspace(EditorDocumentSession::Hud)
                  ->hudDocument()
                  ->json() &&
          restored.workspace(EditorDocumentSession::Hud)
              ->project()
              .world.entities.empty(),
      "HUD recovery was not isolated from the scene world");
  require(readJson(fixture.root / "scenes/main.scene.json") == diskScene &&
              readJson(fixture.prefabSource) == diskPrefab &&
              readJson(fixture.terrainSource) == diskTerrain &&
              readJson(fixture.hudSource) == diskHud,
          "Recovery wrote authored files before Save");
  require(restored.hasUnsavedChanges(), "Recovered source edits are not dirty");
  require(!restored.applyRecovery(snapshot, error),
          "Recovery replaced an already dirty session");
}

void verifyConflictingSourcesAndDraftGuards() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openPrefab(fixture.prefabSource, error), error);
  require(scene.openPrefabDocument(fixture.prefabSource, error), error);
  require(scene.createEntity(error), error);
  auto *prefab = sessions.workspace(EditorDocumentSession::Prefab);
  require(prefab->createEntity(error), error);
  const auto sourceBefore = readJson(fixture.prefabSource);
  require(!sessions.saveAll(error) &&
              error.find("multiple document sessions") != std::string::npos,
          "Conflicting edits of the same source were not detected before Save");
  require(readJson(fixture.prefabSource) == sourceBefore &&
              scene.hasUnsavedChanges() && prefab->hasUnsavedChanges(),
          "Conflict detection wrote a source or discarded a draft");
  require(sessions.dirtyDocuments().size() == 2,
          "Recovery deduplicated conflicting source drafts");
  require(sessions.openPrefab(fixture.prefabSource, error) &&
              sessions.workspace(EditorDocumentSession::Prefab) == prefab,
          "Refocusing the same source discarded its dirty workspace");

  EditorWorkspace cleanScene;
  require(cleanScene.open(fixture.root, error), error);
  EditorDocumentSessions clean(cleanScene);
  require(clean.openTerrainAsset(fixture.terrainSource, error), error);
  auto *terrain = clean.workspace(EditorDocumentSession::TerrainAsset);
  terrain->terrainAuthoring().draft()["seed"] = 999;
  require(clean.hasUnsavedChanges(),
          "An ungenerated graph draft is not pending");
  require(!clean.saveAll(error),
          "Save All silently ignored an ungenerated draft");
  require(!clean.openTerrainAsset(fixture.root / "assets/missing.terrain.json",
                                  error) &&
              clean.workspace(EditorDocumentSession::TerrainAsset) == terrain,
          "A failed document switch discarded an ungenerated graph draft");
}

void verifyProjectSwitchReset() {
  ProjectFixture first;
  ProjectFixture second;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(first.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openPrefab(first.prefabSource, error), error);
  require(sessions.openHud(first.hudSource, error), error);
  require(scene.open(second.root, error), error);
  require(sessions.synchronizeProject(error), error);
  require(sessions.workspace(EditorDocumentSession::Prefab) == nullptr &&
              sessions.workspace(EditorDocumentSession::Hud) == nullptr &&
              &sessions.focused() == &scene,
          "Clean secondary sessions survived a project switch");
  require(sessions.openPrefab(second.prefabSource, error), error);
  auto *prefab = sessions.workspace(EditorDocumentSession::Prefab);
  require(prefab->createEntity(error), error);
  require(scene.open(first.root, error), error);
  require(!sessions.synchronizeProject(error) &&
              sessions.workspace(EditorDocumentSession::Prefab) == prefab &&
              prefab->hasUnsavedChanges(),
          "A project switch discarded dirty secondary-session edits");
}

void verifyIndependentHudEditing() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.focus(EditorDocumentSession::Hud, error), error);
  auto *initial = sessions.workspace(EditorDocumentSession::Hud);
  require(
      initial &&
          initial->hudDocument()->path() == scene.hudDocument()->path() &&
          initial->project().world.entities.empty(),
      "Focusing HUD did not lazily open the scene HUD in its own workspace");
  require(sessions.openHud(fixture.hudSource, error), error);
  auto *hud = sessions.workspace(EditorDocumentSession::Hud);
  require(hud && hud->hudDocument()->path() == fixture.hudSource &&
              hud->activeDocument() == EditorWorkspaceDocument::Hud &&
              hud->project().world.entities.empty(),
          "Opening a different HUD loaded the scene instead of the HUD source");
  require(hud->setHudNodeField("title", "text", "Edited standalone HUD", error),
          error);
  hud->selectHudNode("root");
  require(hud->createHudNode("button", error), error);
  const auto selected = std::string(hud->selectedHudNodeId());
  const auto edited = hud->hudDocument()->json();
  require(sessions.focus(EditorDocumentSession::Scene, error), error);
  require(scene.activateSceneDocument(error), error);
  require(scene.createEntity(error), error);
  require(scene.undo(error), error);
  require(hud->hudDocument()->json() == edited &&
              hud->selectedHudNodeId() == selected &&
              hud->activeDocument() == EditorWorkspaceDocument::Hud &&
              hud->selectedHudNode() != nullptr,
          "Viewport focus or history altered the independent HUD document");
  require(hud->undo(error), error);
  require(hud->hudDocument()->json() != edited,
          "HUD Undo did not operate on its own history");
  require(hud->redo(error), error);
  require(sessions.openHud(fixture.hudSource, error) &&
              sessions.workspace(EditorDocumentSession::Hud) == hud,
          "Reopening a dirty HUD replaced the workspace");
  require(
      !sessions.openHud(fixture.root / "scenes/scene_hud.hud.json", error) &&
          sessions.workspace(EditorDocumentSession::Hud) == hud &&
          hud->hudDocument()->json() == edited,
      "Opening another HUD discarded the dirty HUD document");
  error.clear();
  require(sessions.saveAll(error), error);
  require(!sessions.hasUnsavedChanges() &&
              readJson(fixture.hudSource) == edited,
          "Save All did not publish the independent HUD source");
  require(std::as_const(sessions).workspace(EditorDocumentSession::Hud) ==
                  hud &&
              &std::as_const(sessions).focused() == hud,
          "Const HUD accessors did not preserve the focused document");
}

void verifyHudConflictAndRecoveryFailure() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openHud(fixture.hudSource, error), error);
  require(scene.openHudDocument(fixture.hudSource, error), error);
  require(
      scene.setHudNodeField("title", "text", "Scene workspace draft", error),
      error);
  require(sessions.workspace(EditorDocumentSession::Hud)
              ->setHudNodeField("title", "text", "HUD workspace draft", error),
          error);
  const auto source = readJson(fixture.hudSource);
  require(!sessions.saveAll(error) &&
              error.find("multiple document sessions") != std::string::npos &&
              readJson(fixture.hudSource) == source,
          "Save All did not protect conflicting HUD drafts");
  const auto documents = sessions.dirtyDocuments();
  require(documents.size() == 2 && documents[0].kind == "scene-session:hud" &&
              documents[1].kind == "hud-session:hud",
          "HUD recovery lost the owning session of one conflicting draft");
  EditorWorkspace restoredScene;
  require(restoredScene.open(fixture.root, error), error);
  EditorDocumentSessions restored(restoredScene);
  EditorRecoverySnapshot snapshot{.projectPath = scene.projectPath(),
                                  .documents = documents};
  require(restored.applyRecovery(snapshot, error), error);
  require(restoredScene.openHudDocument(fixture.hudSource, error), error);
  require(restoredScene.hudDocument()->json() == scene.hudDocument()->json() &&
              restored.workspace(EditorDocumentSession::Hud)
                      ->hudDocument()
                      ->json() ==
                  sessions.workspace(EditorDocumentSession::Hud)
                      ->hudDocument()
                      ->json(),
          "Conflicting HUD drafts were not recovered into separate owners");
  require(!restored.saveAll(error),
          "Recovered HUD drafts were silently merged");
}

void verifySavedHudRefreshKeepsSceneHistory() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.focus(EditorDocumentSession::Hud, error), error);
  auto *hud = sessions.workspace(EditorDocumentSession::Hud);
  require(scene.createEntity(error), error);
  const auto sceneSource = scene.sceneDocument().json();
  const auto *entityStorage = scene.project().world.entities.data();
  require(scene.sceneDocument().canUndo(), "Scene has no history to preserve");
  require(hud->setHudNodeField("scene_title", "text", "Saved new HUD", error),
          error);
  const auto editedHud = hud->hudDocument()->json();
  require(sessions.saveAll(error), error);
  require(scene.hudDocument()->json() == editedHud &&
              scene.project().world.ui.nodes.size() ==
                  hud->displayedHud().nodes.size(),
          "Saving the independent HUD left the linked scene HUD cache stale");
  require(scene.sceneDocument().json() == sceneSource &&
              scene.sceneDocument().canUndo() &&
              scene.project().world.entities.data() == entityStorage,
          "Refreshing linked HUD data rebuilt the scene or discarded history");
  require(hud->activeDocumentCanUndo(),
          "Refreshing caches discarded the owning HUD history");
  require(hud->undo(error), error);
  require(hud->saveHud(error), error);
  require(sessions.refreshHudReferences(hud->hudDocument()->path(), error),
          error);
  require(scene.hudDocument()->json() == hud->hudDocument()->json() &&
              scene.project().world.entities.data() == entityStorage,
          "Refreshing after an individual HUD Save rebuilt or left stale scene "
          "data");
}

void verifySceneCreationFromSecondaryWorkspace() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openHud(fixture.hudSource, error), error);
  auto *hud = sessions.workspace(EditorDocumentSession::Hud);
  hud->selectHudNode("title");
  const auto hudSource = hud->hudDocument()->json();
  require(scene.createEntity(error), error);
  const auto sceneSource = scene.sceneDocument().json();
  const auto *sceneStorage = scene.project().world.entities.data();
  fs::path created;
  require(createEditorSource(*hud, EditorSourceKind::Scene3D,
                             "created_from_hud", created, error),
          error);
  require(fs::is_regular_file(created) &&
              hud->project().world.entities.empty() &&
              hud->sceneDocument().path().empty() &&
              hud->hudDocument()->json() == hudSource &&
              hud->selectedHudNodeId() == "title" &&
              hud->activeDocument() == EditorWorkspaceDocument::Hud,
          "Creating a registered scene loaded a world into the HUD session");
  require(sessions.refreshProjectReferences(error), error);
  require(std::ranges::any_of(scene.project().project.scenes,
                              [](const auto &entry) {
                                return entry.id == "scene://created_from_hud";
                              }),
          "Scene metadata did not adopt a registration saved from the HUD");
  require(scene.project().world.entities.data() == sceneStorage &&
              scene.sceneDocument().json() == sceneSource &&
              scene.sceneDocument().canUndo(),
          "Project metadata synchronization rebuilt the scene or lost history");
  require(scene.undo(error), error);
  require(scene.openSceneDocument(created, error), error);
  require(scene.project().world.activeSceneId == "scene://created_from_hud",
          "The main workspace could not open the HUD-created registered scene");

  require(sessions.openPrefab(fixture.prefabSource, error), error);
  auto *prefab = sessions.workspace(EditorDocumentSession::Prefab);
  const auto prefabSource = prefab->sceneDocument().json();
  const auto *prefabStorage = prefab->project().world.entities.data();
  require(createEditorSource(*prefab, EditorSourceKind::Scene2D,
                             "created_from_prefab", created, error),
          error);
  require(sessions.refreshProjectReferences(error), error);
  require(prefab->isPrefabDocument() &&
              prefab->sceneDocument().json() == prefabSource &&
              prefab->project().world.entities.data() == prefabStorage,
          "Registering a scene replaced the independently edited prefab world");
  require(scene.openSceneDocument(created, error), error);
  require(
      scene.project().world.activeSceneId == "scene://created_from_prefab",
      "The main workspace could not open the prefab-created registered scene");
}

void verifyProjectMetadataDoesNotOverwriteDrafts() {
  ProjectFixture fixture;
  EditorWorkspace scene;
  std::string error;
  require(scene.open(fixture.root, error), error);
  EditorDocumentSessions sessions(scene);
  require(sessions.openHud(fixture.hudSource, error), error);
  require(scene.setProjectInputPresets({"wasd_arrows"}, error), error);
  const auto projectDraft = scene.projectDocument().json();
  fs::path created;
  require(createEditorSource(*sessions.workspace(EditorDocumentSession::Hud),
                             EditorSourceKind::Scene2D, "registered_elsewhere",
                             created, error),
          error);
  require(!sessions.refreshProjectReferences(error) &&
              scene.projectDocument().isDirty() &&
              scene.projectDocument().json() == projectDraft,
          "Shared project synchronization discarded unsaved project settings");
  require(scene.projectUndo(error), error);
  error.clear();
  require(sessions.refreshProjectReferences(error), error);
  require(scene.openSceneDocument(created, error), error);
}

} // namespace

int main() {
  try {
    verifyBootstrapAndIndependentHistory();
    verifyRecoveryRoutingAndAtomicFailure();
    verifyConflictingSourcesAndDraftGuards();
    verifyProjectSwitchReset();
    verifyIndependentHudEditing();
    verifyHudConflictAndRecoveryFailure();
    verifySavedHudRefreshKeepsSceneHistory();
    verifySceneCreationFromSecondaryWorkspace();
    verifyProjectMetadataDoesNotOverwriteDrafts();
    std::cout << "Document session tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
