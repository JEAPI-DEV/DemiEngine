#include "editor/EditorSourceCreation.h"
#include "editor/EditorWorkspace.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/gameplay/GameplayDataComponent.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/terrain/TerrainUpdate.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace demi;
using Json = nlohmann::json;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TemporaryProject {
  std::filesystem::path root;
  TemporaryProject() {
    const auto pattern = (std::filesystem::temp_directory_path() /
                          "demi-editor-terrain-asset-XXXXXX")
                             .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    require(created != nullptr, "Could not create temporary project");
    root = created;
  }
  ~TemporaryProject() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error)
      std::cerr << "Could not remove test project: " << error.message() << '\n';
  }
};

void writeJson(const std::filesystem::path &path, const Json &json) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << json.dump(2) << '\n';
  require(output.good(), "Could not write " + path.string());
}

Json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  require(input.good(), "Could not read " + path.string());
  return Json::parse(input);
}

void createProject(const std::filesystem::path &root) {
  writeJson(root / "demi.project.json", Json::parse(R"json({
    "format_version": 1,
    "name": "Terrain asset authoring",
    "main_scene": "scene://terrain/main",
    "scenes": [{"id": "scene://terrain/main",
                "path": "scenes/main.scene.json"}]
  })json"));
  writeJson(root / "scenes/main.scene.json", Json::parse(R"json({
    "format_version": 1,
    "id": "scene://terrain/main",
    "entities": [{
      "id": "camera",
      "components": {
        "Transform3D": {"position": [0, 15, 20]},
        "Camera3D": {"target_offset": [0, -15, -20]}
      }
    }]
  })json"));
}

void awaitTerrain(editor::EditorWorkspace &workspace) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  for (;;) {
    std::string error;
    require(workspace.pollTerrainAuthoring(error), error);
    if (!workspace.terrainAuthoring().pendingEdits())
      return;
    require(std::chrono::steady_clock::now() < deadline,
            "Terrain authoring did not complete");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

const runtime::Terrain3DComponent &terrainIn(const runtime::World &world,
                                             const std::string &id) {
  const auto *entity = runtime::findEntity(world, id);
  const auto *terrain =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  require(terrain && terrain->generated,
          "Terrain preview or scene instance has no prepared field: " + id);
  return *terrain;
}

void raiseTerrain(editor::EditorWorkspace &workspace) {
  auto &authoring = workspace.terrainAuthoring();
  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  authoring.brush.radius = 4;
  authoring.brush.strength = 1;
  authoring.brush.falloff = 0;
  std::string error;
  require(authoring.update({.hovered = true,
                            .focused = true,
                            .leftPressed = true,
                            .leftDown = true},
                           runtime::Vec3{20, 0, 20}, error),
          error);
  require(
      authoring.update({.hovered = true, .focused = true, .leftReleased = true},
                       runtime::Vec3{20, 0, 20}, error),
      error);
  awaitTerrain(workspace);
}

void verifyPinnedAssetGraph() {
  TemporaryProject temporary;
  createProject(temporary.root);
  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);
  std::filesystem::path manifestPath;
  require(editor::createEditorSource(workspace,
                                     editor::EditorSourceKind::Terrain,
                                     "pinned_hills", manifestPath, error),
          error);
  const auto sourcePath =
      workspace.assetIndex().findByManifest(manifestPath)->manifest.sourcePath;
  require(workspace.placeTerrainAsset(manifestPath, runtime::Vec3{}, error),
          error);
  require(workspace.placeTerrainAsset(manifestPath, runtime::Vec3{150, 0, 0},
                                      error),
          error);
  require(workspace.saveAll(error), error);
  const auto originalScene = workspace.sceneDocument().json();
  const auto first = originalScene["entities"][1]["id"].get<std::string>();
  const auto second = originalScene["entities"][2]["id"].get<std::string>();
  workspace.selectEntity(first);
  require(workspace.pinTerrainAuthoring(first, error), error);
  auto &authoring = workspace.terrainAuthoring();
  authoring.draft()["seed"] = 833;
  require(authoring.generate(std::nullopt, error), error);
  workspace.selectEntity("camera");
  require(workspace.terrainEditingAsset() &&
              workspace.terrainAuthoringDocumentPath() == sourcePath &&
              authoring.entityId() == first && !authoring.brushActive(),
          "Selection changed the pinned graph's shared asset source");
  awaitTerrain(workspace);
  require(
      workspace.selectedEntityId() == "camera" &&
          workspace.terrainAssetDocument()->recipe().at("seed") == 833 &&
          terrainIn(workspace.project().world, first).generated ==
              terrainIn(workspace.project().world, second).generated &&
          workspace.sceneDocument().json() == originalScene,
      "Pinned asset generation missed shared owners or mutated scene source");
  require(workspace.undo(error), error);
  require(workspace.selectedEntityId() == "camera" &&
              workspace.terrainAssetDocument()->recipe().at("seed") != 833,
          "Pinned asset Undo used Inspector selection instead of its source");
  require(workspace.redo(error), error);
  require(workspace.terrainAssetDocument()->recipe().at("seed") == 833,
          "Pinned asset Redo missed its source");
  require(workspace.applyTerrainAssetChanges(error), error);
  require(readJson(sourcePath).at("recipe").at("seed") == 833 &&
              readJson(workspace.sceneDocument().path()) == originalScene,
          "Applying a pinned asset wrote the wrong source document");
  require(workspace.unpinTerrainAuthoring(error), error);
  require(authoring.entityId().empty() && !workspace.terrainEditingAsset() &&
              !authoring.brushActive(),
          "Unpin did not restore non-terrain Inspector context");
}

void verifyInSceneTerrainAssetAuthoring() {
  TemporaryProject temporary;
  createProject(temporary.root);
  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);
  std::filesystem::path manifestPath;
  require(editor::createEditorSource(workspace,
                                     editor::EditorSourceKind::Terrain,
                                     "shared_hills", manifestPath, error),
          error);
  const auto *record = workspace.assetIndex().findByManifest(manifestPath);
  const std::string assetId = record->manifest.id;
  const auto sourcePath = record->manifest.sourcePath;
  require(
      workspace.placeTerrainAsset(manifestPath, runtime::Vec3{2, 3, 4}, error),
      error);
  require(workspace.placeTerrainAsset(manifestPath, runtime::Vec3{150, 8, 12},
                                      error),
          error);
  require(workspace.saveAll(error), error);
  const Json sceneBefore = workspace.sceneDocument().json();
  const auto firstId =
      sceneBefore.at("entities").at(1).at("id").get<std::string>();
  const auto secondId =
      sceneBefore.at("entities").at(2).at("id").get<std::string>();
  workspace.selectEntity(firstId);
  require(workspace.terrainEditingAsset() &&
              workspace.activeDocument() ==
                  editor::EditorWorkspaceDocument::Scene &&
              workspace.terrainAuthoringDocumentPath() == sourcePath,
          "Selecting a placed terrain did not bind its source in the scene");
  require(runtime::findEntity(workspace.project().world, "camera") != nullptr &&
              runtime::findEntity(workspace.project().world, secondId) !=
                  nullptr,
          "In-scene terrain editing replaced the scene world");
  const Json originalRecipe = workspace.terrainAssetDocument()->recipe();
  const auto originalField =
      terrainIn(workspace.project().world, firstId).generated;
  raiseTerrain(workspace);
  const auto brushedField =
      terrainIn(workspace.project().world, firstId).generated;
  const Json brushedRecipe = workspace.terrainAssetDocument()->recipe();
  require(brushedRecipe != originalRecipe && brushedField != originalField &&
              brushedField ==
                  terrainIn(workspace.project().world, secondId).generated,
          "A scene stroke did not update both shared terrain owners");
  require(workspace.sceneDocument().json() == sceneBefore &&
              readJson(workspace.sceneDocument().path()) == sceneBefore &&
              readJson(sourcePath).at("recipe") == originalRecipe,
          "Scene brushing wrote source files before Apply");
  const auto *second = runtime::findEntity(workspace.project().world, secondId);
  const auto *transform = second->component<runtime::Transform3DComponent>();
  require(transform->position.x == 150 && transform->position.y == 8 &&
              transform->position.z == 12,
          "Shared asset publication changed a scene placement transform");
  require(workspace.undo(error), error);
  require(
      terrainIn(workspace.project().world, firstId).generated->heights ==
              originalField->heights &&
          terrainIn(workspace.project().world, secondId).generated->heights ==
              originalField->heights,
      "In-scene asset Undo did not restore both placements");
  require(workspace.redo(error), error);
  require(workspace.terrainAssetDocument()->recipe() == brushedRecipe,
          "In-scene asset Redo did not restore its recipe");

  // The latest edit chooses its history owner even while Terrain stays
  // selected.
  require(workspace.editValue({.entityId = firstId,
                               .component = "Transform3D",
                               .field = "position"},
                              Json::array({12, 13, 14}), false, error),
          error);
  require(workspace.terrainEditingAsset() && workspace.activeDocumentCanUndo(),
          "Moving a selected Terrain lost the source binding or scene history");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == sceneBefore &&
              workspace.terrainAssetDocument()->recipe() == brushedRecipe,
          "Placement Undo incorrectly consumed the asset brush history");
  require(workspace.redo(error), error);
  require(
      runtime::findEntity(workspace.project().world, firstId)
                  ->component<runtime::Transform3DComponent>()
                  ->position.x == 12 &&
          terrainIn(workspace.project().world, firstId).generated->heights ==
              brushedField->heights,
      "Placement Redo lost the asset preview or restored the wrong history");
  require(workspace.undo(error), error);

  // A scene command forces the normal loader/rebuild path. It must keep the
  // unsaved source asset preview without borrowing its history.
  workspace.selectEntity("camera");
  require(!workspace.terrainEditingAsset() &&
              workspace.terrainAuthoringDocumentPath() ==
                  workspace.sceneDocument().path(),
          "An unrelated scene selection retained asset Undo/Save routing");
  require(workspace.createEntity(error), error);
  require(
      terrainIn(workspace.project().world, firstId).generated->heights ==
              brushedField->heights &&
          terrainIn(workspace.project().world, secondId).generated->heights ==
              brushedField->heights,
      "A scene rebuild reverted unsaved asset edits");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == sceneBefore &&
              workspace.terrainAssetDocument()->recipe() == brushedRecipe,
          "Scene Undo consumed terrain asset history");
  workspace.selectEntity(secondId);
  require(workspace.terrainEditingAsset(),
          "The second owner did not rebind the shared document");

  // A generation draft belongs to the shared source, not the selected owner.
  workspace.terrainAuthoring().draft()["seed"] =
      originalRecipe.at("seed").get<int>() + 17;
  const auto draftSeed = workspace.terrainAuthoring().draft().at("seed");
  workspace.selectEntity("camera");
  workspace.selectEntity(firstId);
  require(workspace.terrainAuthoring().draft().at("seed") == draftSeed,
          "Selecting another owner lost the shared generation draft");
  require(workspace.createEntity(error), error);
  workspace.selectEntity(secondId);
  require(
      workspace.terrainAuthoring().draft().at("seed") == draftSeed &&
          terrainIn(workspace.project().world, secondId).generated->heights ==
              brushedField->heights,
      "An ordinary scene rebuild lost the terrain draft or applied preview");
  workspace.terrainAuthoring().discardDraft();
  workspace.selectEntity("camera");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == sceneBefore,
          "Independent scene history changed after draft editing");
  require(workspace.applyTerrainAssetChanges(error), error);
  require(readJson(sourcePath).at("recipe") == brushedRecipe &&
              workspace.sceneDocument().json() == sceneBefore,
          "Apply did not save the retained asset document independently of the "
          "scene");
  auto registry = loadAssetRegistry(temporary.root);
  require(assets::loadTerrainAsset(registry, assetId)->heights ==
              brushedField->heights,
          "Applied in-scene edits disagree with prepared asset data");

  // Standalone mode is still optional, and it returns to the original scene.
  workspace.selectEntity(firstId);
  require(workspace.openTerrainAssetDocument(sourcePath, error), error);
  require(workspace.activeDocument() ==
              editor::EditorWorkspaceDocument::TerrainAsset,
          "The standalone asset stage no longer opens");
  require(workspace.activateSceneDocument(error), error);
  require(runtime::findEntity(workspace.project().world, "camera") &&
              workspace.sceneDocument().json() == sceneBefore,
          "Leaving the optional asset stage did not restore the scene");

  // An unsaved source is retained or the document switch is explicitly refused.
  workspace.selectEntity(firstId);
  raiseTerrain(workspace);
  const Json unsavedSource = workspace.terrainAssetDocument()->json();
  std::filesystem::path otherManifest;
  require(editor::createEditorSource(workspace,
                                     editor::EditorSourceKind::Terrain,
                                     "other_hills", otherManifest, error),
          error);
  const auto otherSource =
      workspace.assetIndex().findByManifest(otherManifest)->manifest.sourcePath;
  error.clear();
  require(!workspace.openTerrainAssetDocument(otherSource, error) &&
              !error.empty() &&
              workspace.terrainAssetDocument()->json() == unsavedSource &&
              workspace.activeDocument() ==
                  editor::EditorWorkspaceDocument::Scene,
          "Switching Terrain assets discarded an unsaved source document");
  error.clear();
  require(!workspace.open(temporary.root, error) && !error.empty() &&
              workspace.terrainAssetDocument()->json() == unsavedSource,
          "Opening a project discarded the retained Terrain document");
  error.clear();
  require(workspace.saveAll(error), error);
  require(readJson(sourcePath) == unsavedSource &&
              readJson(workspace.sceneDocument().path()) == sceneBefore,
          "Save All did not save the independent retained asset document");
}

void writeAsset(const std::filesystem::path &root, const std::string &name,
                const std::string &id, const std::string &type,
                const std::string &importer, const Json &source,
                const Json &settings = Json::object(),
                const Json &dependencies = Json::array()) {
  const auto sourceName =
      name + (type == "Terrain" ? ".terrain.json" : ".json");
  const auto sourcePath = root / "assets" / sourceName;
  writeJson(sourcePath, source);
  const auto hash = assets::hashFiles({sourcePath});
  require(hash.has_value(), "Could not hash terrain palette test source");
  writeJson(root / "assets" / (name + ".asset.json"),
            {{"format_version", 1},
             {"id", id},
             {"type", type},
             {"importer", importer},
             {"importer_version", 1},
             {"source", sourceName},
             {"source_hash", *hash},
             {"settings", settings},
             {"dependencies", dependencies}});
}

void verifyPrefabScatterInScene() {
  TemporaryProject temporary;
  createProject(temporary.root);
  const std::string materialId = "asset://test/tree_material";
  const std::string paletteId = "asset://test/palette";
  const std::string terrainId = "asset://test/terrain";
  writeAsset(temporary.root, "tree_material", materialId, "Material",
             "material",
             {{"format_version", 1},
              {"shader", "builtin://lit"},
              {"parameters", {{"base_color", {0.2, 0.4, 0.2, 1}}}}});
  writeJson(temporary.root / "prefabs/tree.prefab.json", Json::parse(R"json({
    "format_version": 1, "id": "prefab://tree",
    "entities": [{"id": "body", "components": {
      "Transform3D": {}, "MeshRenderer": {"shape": "cube"},
      "GameplayData": {"values": {"counter": 0}}
    }}]
  })json"));
  writeAsset(temporary.root, "palette", paletteId, "DataAsset", "json_data",
             {{"format_version", 1},
              {"name", "Prefab palette"},
              {"roles",
               {{"tree",
                 {{"asset", materialId},
                  {"prefab", "prefab://tree"},
                  {"spacing", 0},
                  {"scale", {1, 1}}}}}}},
             {{"content_type", "terrain_palette"}}, Json::array({materialId}));
  auto recipe = runtime::TerrainRecipe{};
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.chunkCells = 2;
  recipe.landforms.at("default").baseHeight = 2;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.paletteId = paletteId;
  writeAsset(
      temporary.root, "terrain", terrainId, "Terrain", "terrain_heightfield",
      {{"format_version", 1}, {"id", terrainId}, {"recipe", recipe.toJson()}},
      Json::object(), Json::array({paletteId}));
  auto scene = readJson(temporary.root / "scenes/main.scene.json");
  scene["entities"].push_back({{"id", "land"},
                               {"components",
                                {{"Transform3D", {{"position", {10, 0, 20}}}},
                                 {"Terrain3D", {{"asset", terrainId}}}}}});
  writeJson(temporary.root / "scenes/main.scene.json", scene);
  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);
  const auto preparedField =
      terrainIn(workspace.project().world, "land").generated;
  const auto placement = preparedField->scatterPlacements.at(12);
  const auto instanceId = runtime::terrainScatterInstanceId(
      "land", paletteId, placement.role, placement.cell);
  const std::string rootId = instanceId + "/body";
  auto *root = runtime::findEntity(workspace.project().world, rootId);
  require(root && root->prefabInstance == instanceId,
          "Prefab scenery was not materialized during initial scene load");
  root->component<runtime::GameplayDataComponent>()->valuesJson =
      "{\"counter\":42}";
  workspace.selectEntity("land");
  auto &authoring = workspace.terrainAuthoring();
  const runtime::Vec3 hit = placement.position;
  const auto stroke = [&](editor::EditorTerrainBrush mode) {
    authoring.brush.mode = mode;
    authoring.brush.radius = 0.6F;
    authoring.brush.strength = 1;
    authoring.brush.falloff = 0;
    require(authoring.update({.hovered = true,
                              .focused = true,
                              .leftPressed = true,
                              .leftDown = true},
                             hit, error),
            error);
    require(authoring.update(
                {.hovered = true, .focused = true, .leftReleased = true}, hit,
                error),
            error);
    awaitTerrain(workspace);
  };
  stroke(editor::EditorTerrainBrush::Raise);
  root = runtime::findEntity(workspace.project().world, rootId);
  require(root && root->prefabInstance == instanceId &&
              root->component<runtime::GameplayDataComponent>()->valuesJson ==
                  "{\"counter\":42}" &&
              root->component<runtime::Transform3DComponent>()->position.y >
                  placement.position.y &&
              root->component<runtime::Transform3DComponent>()->parent ==
                  "land",
          "A first sculpt rebuilt prefab scenery, lost its state, or ignored "
          "the terrain transform");
  stroke(editor::EditorTerrainBrush::Exclusion);
  require(!runtime::findEntity(workspace.project().world, rootId),
          "An exclusion stroke failed to release the existing prefab instance");
  require(workspace.undo(error), error);
  require(
      runtime::findEntity(workspace.project().world, rootId) &&
          runtime::findEntity(workspace.project().world, rootId)
                  ->prefabInstance == instanceId,
      "Exclusion Undo failed to restore the stable prefab placement identity");
  require(workspace.openTerrainAssetDocument(
              temporary.root / "assets/terrain.terrain.json", error),
          error);
  require(workspace.activateSceneDocument(error), error);
  // The optional isolated stage has independent prefab bookkeeping. Returning
  // to the scene must still allow its original palette instances to release.
  workspace.selectEntity("land");
  stroke(editor::EditorTerrainBrush::Exclusion);
  require(!runtime::findEntity(workspace.project().world, rootId),
          "An isolated asset stage discarded the scene's prefab service state");
  require(workspace.sceneDocument().json() == scene,
          "Prefab scatter editing wrote terrain recipes into the scene");
}

void verifyTerrainAssetAuthoring() {
  TemporaryProject temporary;
  createProject(temporary.root);
  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);

  std::filesystem::path manifestPath;
  require(editor::createEditorSource(workspace,
                                     editor::EditorSourceKind::Terrain, "hills",
                                     manifestPath, error),
          error);
  const auto *asset = workspace.assetIndex().findByManifest(manifestPath);
  require(asset && asset->manifest.type == "Terrain",
          "The Terrain source was not imported as a Terrain asset");
  const std::filesystem::path sourcePath = asset->manifest.sourcePath;
  const std::string assetId = asset->manifest.id;
  require(readJson(sourcePath).at("id") == assetId,
          "The created source ID differs from its manifest");

  require(
      workspace.placeTerrainAsset(manifestPath, runtime::Vec3{0, 0, 0}, error),
      error);
  require(workspace.placeTerrainAsset(manifestPath, runtime::Vec3{150, 0, 0},
                                      error),
          error);
  require(workspace.save(error), error);
  const Json sceneBeforeEditing = workspace.sceneDocument().json();
  const auto &placed = sceneBeforeEditing.at("entities");
  require(placed.size() == 3, "Placing twice should create two scene owners");
  const std::string firstId = placed.at(1).at("id");
  const std::string secondId = placed.at(2).at("id");
  require(placed.at(1).at("components").at("Terrain3D") ==
                  Json{{"asset", assetId}} &&
              placed.at(2).at("components").at("Terrain3D") ==
                  Json{{"asset", assetId}},
          "Scene placements must contain only stable asset references");

  require(workspace.openTerrainAssetDocument(sourcePath, error), error);
  require(workspace.activeDocument() ==
              editor::EditorWorkspaceDocument::TerrainAsset,
          "Opening the source did not activate the Terrain stage");
  require(workspace.sceneDocument().json() == sceneBeforeEditing,
          "Opening an asset changed the authored scene");
  workspace.syncTerrainAuthoring();
  auto &authoring = workspace.terrainAuthoring();
  require(authoring.surface() != nullptr,
          "A cold Terrain asset did not load its prepared preview");
  const Json originalRecipe = workspace.terrainAssetDocument()->recipe();
  const auto coldField =
      terrainIn(workspace.project().world, "terrain").generated;
  const auto before = runtime::TerrainRecipe::parse(originalRecipe);
  auto after = before;
  after.edits.push_back({.kind = runtime::TerrainEditKind::Raise,
                         .center = {20, 20},
                         .radius = 4,
                         .falloff = 0});
  const auto inputs = assets::resolveTerrainAssetGenerationInputs(
      workspace.assetIndex().registry(), before);
  require(coldField->inputFingerprint == inputs.fingerprint,
          "Cold editor preview lost the asset dependency fingerprint");
  const auto local =
      runtime::updateTerrainWithInputs(before, after, coldField, inputs);
  require(local && !local->invalidation.fullGeneration,
          "The first brush edit regenerated the whole prepared Terrain asset");

  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  authoring.brush.radius = 4.0F;
  authoring.brush.strength = 1.0F;
  authoring.brush.falloff = 0.0F;
  require(authoring.update({.hovered = true,
                            .focused = true,
                            .leftPressed = true,
                            .leftDown = true},
                           runtime::Vec3{20, 0, 20}, error),
          error);
  require(
      authoring.update({.hovered = true, .focused = true, .leftReleased = true},
                       runtime::Vec3{20, 0, 20}, error),
      error);
  awaitTerrain(workspace);
  const Json brushedRecipe = workspace.terrainAssetDocument()->recipe();
  require(brushedRecipe.at("edits").size() ==
              originalRecipe.at("edits").size() + 1,
          "Cold brush did not add an asset owned edit");
  require(workspace.undo(error), error);
  require(workspace.terrainAssetDocument()->recipe() == originalRecipe,
          "Brush Undo did not restore the asset recipe");
  require(workspace.redo(error), error);
  require(workspace.terrainAssetDocument()->recipe() == brushedRecipe,
          "Brush Redo did not restore the asset recipe");
  require(workspace.sceneDocument().json() == sceneBeforeEditing,
          "Terrain asset history wrote the scene document");

  workspace.syncTerrainAuthoring();
  authoring.draft()["seed"] = brushedRecipe.at("seed").get<int>() + 1;
  require(authoring.generate(std::nullopt, error), error);
  awaitTerrain(workspace);
  const Json generatedRecipe = workspace.terrainAssetDocument()->recipe();
  require(generatedRecipe != brushedRecipe &&
              generatedRecipe.at("edits") == brushedRecipe.at("edits") &&
              workspace.sceneDocument().json() == sceneBeforeEditing,
          "Generate did not preserve asset owned brush edits");

  const auto preview =
      terrainIn(workspace.project().world, "terrain").generated;
  require(workspace.save(error), error);
  require(readJson(sourcePath).at("recipe") == generatedRecipe,
          "Save did not persist the edited Terrain source");
  const AssetRegistry registry = loadAssetRegistry(temporary.root);
  const auto savedField = assets::loadTerrainAsset(registry, assetId);
  require(savedField && savedField->heights == preview->heights,
          "The saved source and prepared terrain cache disagree");
  require(workspace.activateSceneDocument(error), error);
  require(workspace.sceneDocument().json() == sceneBeforeEditing,
          "Closing the Terrain stage changed the scene source");
  const auto &first = terrainIn(workspace.project().world, firstId);
  const auto &second = terrainIn(workspace.project().world, secondId);
  require(first.generated->heights == savedField->heights &&
              second.generated->heights == savedField->heights &&
              first.asset == assetId && second.asset == assetId,
          "Saved asset edits did not reach both scene placements");
}

void verifyInlineExtraction() {
  TemporaryProject temporary;
  createProject(temporary.root);
  auto scene = readJson(temporary.root / "scenes/main.scene.json");
  const Json recipe = runtime::TerrainRecipe::defaults();
  scene["entities"].push_back({{"id", "procedural"},
                               {"components",
                                {{"Transform3D", Json::object()},
                                 {"Terrain3D", {{"recipe", recipe}}}}}});
  writeJson(temporary.root / "scenes/main.scene.json", scene);

  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);
  std::filesystem::path manifestPath;
  require(editor::createEditorSource(
              workspace, editor::EditorSourceKind::TerrainFromSelection,
              "extracted", manifestPath, error, "procedural"),
          error);
  const auto *asset = workspace.assetIndex().findByManifest(manifestPath);
  require(asset && readJson(asset->manifest.sourcePath).at("recipe") == recipe,
          "Extraction did not preserve the procedural recipe");
  require(workspace.sceneDocument()
                  .component("procedural", "Terrain3D")
                  ->at("asset") == asset->manifest.id,
          "Extraction did not make a compact scene reference");
  require(!workspace.sceneDocument()
               .component("procedural", "Terrain3D")
               ->contains("recipe"),
          "Extraction left a second inline recipe in the scene");
  workspace.selectEntity("camera");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument()
                  .component("procedural", "Terrain3D")
                  ->at("recipe") == recipe,
          "Extraction Undo did not restore the inline procedural recipe");
}

void verifyPreparedTerrainIsReadOnly() {
  TemporaryProject temporary;
  createProject(temporary.root);
  runtime::TerrainRecipe recipe;
  recipe.size = {4, 4};
  recipe.cellsX = recipe.cellsZ = 4;
  const auto field = runtime::TerrainGenerator::generate(recipe);
  require(field.has_value(), "Could not generate prepared fixture");
  const auto payload = assets::serializeTerrainAssetPayload(
      *field, runtime::terrainCookRecipeDigest(recipe), "prepared-test");
  const auto folder = temporary.root / "assets/terrain/prepared";
  std::filesystem::create_directories(folder);
  {
    std::ofstream output(folder / "prepared.terrain.bin", std::ios::binary);
    output.write(reinterpret_cast<const char *>(payload.data()),
                 payload.size());
    require(output.good(), "Could not write prepared terrain fixture");
  }
  const auto hash = assets::hashFiles({folder / "prepared.terrain.bin"});
  require(hash.has_value(), "Could not hash prepared terrain fixture");
  writeJson(folder / "prepared.asset.json",
            {{"format_version", 1},
             {"id", "asset://terrain/prepared"},
             {"type", "Terrain"},
             {"importer", "terrain_heightfield"},
             {"importer_version", 1},
             {"source", "prepared.terrain.bin"},
             {"source_hash", *hash},
             {"dependencies", Json::array()},
             {"settings", Json::object()}});
  writeJson(
      temporary.root / "scenes/main.scene.json",
      {{"format_version", 1},
       {"id", "scene://terrain/main"},
       {"entities",
        Json::array(
            {{{"id", "terrain"},
              {"components",
               {{"Transform3D", Json::object()},
                {"Terrain3D", {{"asset", "asset://terrain/prepared"}}}}}}})}});
  editor::EditorWorkspace workspace;
  std::string error;
  require(workspace.open(temporary.root, error), error);
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  require(!hasErrors(workspace.diagnostics()),
          "Prepared terrain selection tried to parse binary authoring data");
  require(workspace.terrainAuthoring().entityId().empty(),
          "Prepared terrain was bound to editable brushes");
  require(!workspace.pinTerrainAuthoring("terrain", error) &&
              error.find("read-only") != std::string::npos,
          "Prepared terrain graph opening lacked a read-only diagnostic");
}
} // namespace

int main() {
  try {
    const auto run = [](const char *name, void (*test)()) {
      try {
        test();
      } catch (const std::exception &failure) {
        throw std::runtime_error(std::string(name) + ": " + failure.what());
      }
    };
    run("in-scene asset authoring", verifyInSceneTerrainAssetAuthoring);
    run("pinned shared asset graph", verifyPinnedAssetGraph);
    run("prefab scatter in scene", verifyPrefabScatterInScene);
    run("standalone asset authoring", verifyTerrainAssetAuthoring);
    run("inline extraction", verifyInlineExtraction);
    run("prepared terrain read-only", verifyPreparedTerrainIsReadOnly);
    std::cout << "Terrain asset editor authoring passed\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
