#include "editor/EditorScenePreview.h"
#include "editor/EditorTerrainPicking.h"
#include "editor/EditorViewportProjection.h"
#include "editor/EditorWorkspace.h"

#include "demi/assets/AssetHash.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace demi;
using Json = nlohmann::json;

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const std::filesystem::path &path, const Json &document) {
  std::ofstream output(path);
  output << document.dump(2) << '\n';
  output.close();
  check(!output.fail(), "Could not write terrain fixture: " + path.string());
}

Json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  check(input.good(), "Could not read terrain fixture: " + path.string());
  return Json::parse(input);
}

// Own only a newly created directory, never a fixed shared temporary path.
struct TemporaryProject {
  std::filesystem::path root;
  TemporaryProject() {
    const auto pattern = (std::filesystem::temp_directory_path() /
                          "demi-editor-terrain-workspace-XXXXXX")
                             .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    check(created != nullptr, "Could not create temporary terrain project");
    root = created;
  }
  ~TemporaryProject() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean terrain test fixture: " << error.message()
                << '\n';
  }
  TemporaryProject(const TemporaryProject &) = delete;
  TemporaryProject &operator=(const TemporaryProject &) = delete;
};

// A preset is an ordinary DataAsset: the manifest declares its stable id and
// its terrain_preset content type, and the source is the preset document. The
// manifest carries the real source hash because the project validator refuses
// to load a project whose assets are stale.
void writePresetAsset(const std::filesystem::path &root, const std::string &name,
                      const Json &document) {
  const auto directory = root / "assets/terrain/presets";
  std::filesystem::create_directories(directory);
  const auto source = directory / (name + ".json");
  writeJson(source, document);
  const Json manifest = {
      {"format_version", 1},
      {"id", "asset://terrain/presets/" + name},
      {"type", "DataAsset"},
      {"importer", "json_data"},
      {"importer_version", 1},
      {"source_hash", *assets::hashFiles({source})},
      {"dependencies", Json::array()},
      {"source", name + ".json"},
      {"settings", {{"content_type", "terrain_preset"}}}};
  writeJson(directory / (name + ".asset.json"), manifest);
}

// The fixture terrain paints a "hill" region, so a preset that can be applied
// to it has to keep that biome available. A preset is free to add its own. The
// document declares the canonical layer set because the loader validates the
// assembled preset against the same recipe rules as an authored document, and
// the recipe owns the only accepted layer kind names.
// Written with named locals rather than one nested literal: the landform and
// biome maps are separate objects now, and a single dense initializer is
// unreadable and easy to misbalance.
Json alpinePreset(const std::string &name, int seed, int cells = 8,
                  bool keepHill = true) {
  const float extent = cells == 8 ? 8.8F : 17.6F;
  const auto shape = [](float base, float variation, float feature) {
    return Json{{"base_height", base},
                {"height_variation", variation},
                {"feature_size", feature},
                {"roughness", 0.45},
                {"octaves", 3}};
  };
  const auto surface = [](const char *landform, double r, double g, double b) {
    return Json{{"landform", landform}, {"color", {r, g, b, 1.0}}};
  };

  Json landforms{{"default", shape(0, 4, 16)},
                 {"rock", shape(9, 6, 18)},
                 {"scree", shape(3, 2, 10)}};
  Json biomes{{"default", surface("default", 0.2, 0.5, 0.2)},
              {"rock", surface("rock", 0.5, 0.5, 0.5)},
              {"scree", surface("scree", 0.6, 0.6, 0.55)}};
  if (keepHill) {
    landforms["hill"] = shape(4, 3, 12);
    biomes["hill"] = surface("hill", 0.3, 0.5, 0.2);
  }

  Json recipe{{"format_version", 1},
              {"name", "Alpine ridge"},
              {"label", "Alpine"},
              {"description", name},
              {"size", {extent, extent}},
              {"cells_x", cells},
              {"cells_z", cells},
              {"chunk_cells", 4},
              {"seed", seed},
              {"default_biome", "rock"},
              {"default_landform", "default"},
              {"landforms", std::move(landforms)},
              {"biomes", std::move(biomes)},
              {"layers",
               Json::array({{{"id", "generation"}, {"name", "Generation"},
                             {"kind", "generation"}, {"enabled", true}},
                            {{"id", "biomes"}, {"name", "Biomes"},
                             {"kind", "biome"}, {"enabled", true}},
                            {{"id", "sculpt"}, {"name", "Sculpt"},
                             {"kind", "sculpt"}, {"enabled", true}},
                            {{"id", "protection"}, {"name", "Protection"},
                             {"kind", "protection"}, {"enabled", true}},
                            {{"id", "exclusions"}, {"name", "Exclusions"},
                             {"kind", "exclusion"}, {"enabled", true}}})}};
  return recipe;
}

void createProject(const std::filesystem::path &root) {
  runtime::TerrainRecipe recipe;
  recipe.size = {8.8F, 8.8F};
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.chunkCells = 4;
  // A biome names a shape; the shape itself is what has elevation.
  recipe.landforms.at("default").featureSize = 3;
  recipe.landforms.at("default").heightVariation = 1;
  recipe.landforms.emplace("hill", runtime::TerrainLandform{.baseHeight = 4,
                                                            .heightVariation = 1,
                                                            .featureSize = 3});
  recipe.biomes.emplace("hill", runtime::TerrainBiome{.landform = "hill"});
  recipe.regions.push_back({.biome = "hill", .center = {7, 7}, .radius = 2});
  recipe.edits.push_back({.kind = runtime::TerrainEditKind::Raise,
                          .center = {1, 1},
                          .radius = 1,
                          .amount = 3});
  recipe.edits.push_back({.kind = runtime::TerrainEditKind::Flatten,
                          .center = {4, 4},
                          .radius = 2,
                          .falloff = 0,
                          .targetHeight = 2});
  std::filesystem::create_directory(root / "scenes");
  const Json project = Json::parse(R"({
    "format_version": 1,
    "name": "Terrain workspace contract",
    "main_scene": "scene://terrain_workspace/main",
    "scenes": [{"id": "scene://terrain_workspace/main",
                "path": "scenes/main.scene.json"}]
  })");
  Json scene = Json::parse(R"({
    "format_version": 1,
    "id": "scene://terrain_workspace/main",
    "entities": [
      {"id": "terrain", "name": "Authored terrain", "components": {
        "Transform3D": {}, "Terrain3D": {"recipe": {}}
      }},
      {"id": "building", "name": "Independent object", "components": {
        "Transform3D": {"position": [20, 6, 5]},
        "MeshRenderer": {"shape": "cube", "size": [2, 4, 2]},
        "BoxCollider3D": {"size": [2, 4, 2]}
      }},
      {"id": "camera", "components": {
        "Transform3D": {"position": [4, 18, 14]},
        "Camera3D": {"target_offset": [0, -16, -10], "far_clip": 100}
      }}
    ]
  })");
  scene["entities"][0]["components"]["Terrain3D"]["recipe"] = recipe.toJson();
  writeJson(root / "demi.project.json", project);
  writeJson(root / "scenes/main.scene.json", scene);
}

const runtime::Terrain3DComponent &
terrain(const editor::EditorWorkspace &workspace,
        std::source_location caller = std::source_location::current()) {
  const auto *entity =
      runtime::findEntity(workspace.project().world, "terrain");
  const auto *component =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  if (!component || !component->generated) {
    std::ostringstream message;
    message << "Workspace terrain lookup failed at " << caller.file_name()
            << ':' << caller.line() << " in " << caller.function_name()
            << "; expected_id=terrain, entity="
            << (entity ? "present" : "missing")
            << ", component=" << (component ? "present" : "missing")
            << ", generated="
            << (component && component->generated ? "present" : "missing")
            << ", selected=" << workspace.selectedEntityId()
            << ", prefab=" << workspace.isPrefabDocument()
            << ", document=" << workspace.sceneDocument().path();
    if (component && component->recipe.is_object())
      message << ", seed=" << component->recipe.value("seed", 1337);
    std::size_t surfaces = 0;
    for (const auto &candidate : workspace.project().world.entities) {
      surfaces += runtime::terrainSurfaceOwner(candidate) == "terrain";
      if (candidate.hasComponent<runtime::Terrain3DComponent>())
        message << ", terrain_entity=" << candidate.id;
    }
    message << ", generated_surfaces=" << surfaces;
    throw std::runtime_error(message.str());
  }
  for (const auto &candidate : workspace.project().world.entities) {
    if (runtime::terrainSurfaceOwner(candidate) == "terrain")
      check(editor::editorPlacementOwner(workspace.project().world,
                                         candidate.id) == "terrain",
            "Generated terrain surface must redirect to its authored owner");
  }
  return *component;
}

void selectTerrain(editor::EditorWorkspace &workspace) {
  workspace.setViewDimension(
      editor::EditorSceneViewDimension::ThreeDimensional);
  workspace.selectEntity("terrain");
  check(workspace.sceneView().frameEntity(workspace.project().world, "terrain"),
        "Could not frame the terrain owner");
  check(workspace.sceneView().camera().projection.orthographicSize >
            terrain(workspace).generated->size.x,
        "Terrain framing used the owner placeholder instead of its surface");
  workspace.syncTerrainAuthoring();
  check(workspace.terrainAuthoring().entityId() == "terrain" &&
            workspace.terrainAuthoring().surface(),
        "Workspace did not bind selected terrain to its real surface");
}

void pollCompletion(editor::EditorWorkspace &workspace) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  std::string error;
  do {
    check(workspace.pollTerrainAuthoring(error),
          "Terrain polling failed: " + error);
    check(error.empty(), "Unexpected terrain polling error: " + error);
    if (!workspace.terrainAuthoring().busy())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("Workspace terrain generation exceeded its timeout");
}

void checkIndependentObject(const editor::EditorWorkspace &workspace,
                            const Json &authored) {
  const auto *source = workspace.sceneDocument().entity("building");
  check(source && *source == authored,
        "Terrain operation changed independent authored object");
  const auto *entity =
      runtime::findEntity(workspace.project().world, "building");
  const auto *transform =
      entity ? entity->component<runtime::Transform3DComponent>() : nullptr;
  check(transform && transform->position.x == 20 &&
            transform->position.y == 6 && transform->position.z == 5 &&
            entity->hasComponent<runtime::BoxCollider3DComponent>(),
        "Terrain operation relocated or removed independent runtime object");
}

void testGenerateRegenerateHistoryAndSave(const std::filesystem::path &root) {
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(root, error), "Workspace open failed: " + error);
  selectTerrain(workspace);
  const Json initialDocument = workspace.sceneDocument().json();
  const Json initialRecipe = terrain(workspace).recipe;
  const Json building = *workspace.sceneDocument().entity("building");
  const auto initialField = terrain(workspace).generated;
  auto &authoring = workspace.terrainAuthoring();
  authoring.draft()["seed"] = 42;
  const Json generatedRecipe = authoring.draft();
  check(authoring.generate(std::nullopt, error), "Generate failed: " + error);
  check(terrain(workspace).generated == initialField &&
            workspace.sceneDocument().json() == initialDocument,
        "Worker replaced preview or authored document before workspace "
        "polling");
  pollCompletion(workspace);
  check(terrain(workspace).recipe == generatedRecipe &&
            workspace.sceneDocument().isDirty(),
        "Workspace polling did not commit generated recipe");
  const auto generatedField = terrain(workspace).generated;
  check(generatedField != initialField &&
            generatedField->heights != initialField->heights,
        "Generate did not replace the visible native surface");
  check(terrain(workspace).recipe["edits"] == initialRecipe["edits"] &&
            terrain(workspace).recipe["regions"] == initialRecipe["regions"] &&
            generatedField->height(4, 4) == 2,
        "Generate discarded existing sculpt/biome edits");
  checkIndependentObject(workspace, building);

  check(workspace.undo(error), "Generate Undo failed: " + error);
  check(workspace.sceneDocument().json() == initialDocument &&
            terrain(workspace).generated->heights == initialField->heights,
        "Undo did not restore both authored recipe and visible surface");
  check(workspace.redo(error), "Generate Redo failed: " + error);
  check(terrain(workspace).recipe == generatedRecipe &&
            terrain(workspace).generated->heights == generatedField->heights,
        "Redo did not restore both authored recipe and visible surface");

  // Drive the actual workspace brush/picking path, not a fabricated surface
  // hit.
  selectTerrain(workspace);
  check(workspace.sceneView().alignToFirstCamera(workspace.project().world),
        "Could not align the fixture viewport camera");
  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  authoring.brush.radius = 1;
  authoring.brush.strength = 1;
  authoring.brush.falloff = 0;
  const editor::EditorViewportToolInput press{.mousePosition = {400, 300},
                                              .viewportSize = {800, 600},
                                              .hovered = true,
                                              .focused = true,
                                              .leftPressed = true,
                                              .leftDown = true};
  const auto hit = editor::pickEditorTerrain(
      workspace.project().world, "terrain", authoring.surface(),
      workspace.sceneView().camera(), press);
  check(hit.has_value(), "Fixture camera did not hit the real terrain surface");
  check(workspace.updateViewportTool(press, error),
        "Workspace brush press failed: " + error);
  check(authoring.stroking(), "Workspace brush did not start a terrain stroke");
  check(workspace.updateViewportTool({.mousePosition = {400, 300},
                                      .viewportSize = {800, 600},
                                      .hovered = true,
                                      .focused = true,
                                      .leftReleased = true},
                                     error),
        "Workspace brush release failed: " + error);
  pollCompletion(workspace);
  const Json sculptedRecipe = terrain(workspace).recipe;
  const auto sculptedField = terrain(workspace).generated;
  check(sculptedRecipe["edits"].size() == generatedRecipe["edits"].size() + 1 &&
            sculptedField->heights != generatedField->heights,
        "Real workspace sculpt stroke did not commit an edit and surface");
  check(workspace.undo(error), "Sculpt Undo failed: " + error);
  check(terrain(workspace).recipe == generatedRecipe &&
            terrain(workspace).generated->heights == generatedField->heights,
        "Sculpt Undo did not restore surface");
  check(workspace.redo(error), "Sculpt Redo failed: " + error);
  check(terrain(workspace).recipe == sculptedRecipe &&
            terrain(workspace).generated->heights == sculptedField->heights,
        "Sculpt Redo did not restore surface");

  selectTerrain(workspace);
  authoring.brush.mode = editor::EditorTerrainBrush::Protect;
  authoring.brush.radius = 2;
  check(workspace.updateViewportTool(press, error),
        "Workspace Protect press failed: " + error);
  check(authoring.stroking(), "Protect did not hit the real surface");
  check(workspace.updateViewportTool({.mousePosition = {400, 300},
                                      .viewportSize = {800, 600},
                                      .hovered = true,
                                      .focused = true,
                                      .leftReleased = true},
                                     error),
        "Workspace Protect release failed: " + error);
  const Json protectedRecipe = authoring.draft();
  const Json protection = protectedRecipe["edits"].back();
  check(protection["type"] == "protect" &&
            !protection["snapshot"]["samples"].empty(),
        "Real Protect stroke did not capture height samples");
  bool hasSubMillimeterPrecision = false;
  for (const auto &sample : protection["snapshot"]["samples"]) {
    for (const auto &coordinate : sample["position"]) {
      const double value = coordinate.get<double>();
      hasSubMillimeterPrecision |=
          std::abs(value - std::round(value * 1000) / 1000) > 1e-9;
    }
  }
  check(hasSubMillimeterPrecision,
        "Protection fixture does not exercise noninteger grid precision");
  pollCompletion(workspace);
  check(terrain(workspace).recipe == protectedRecipe &&
            terrain(workspace).generated ==
                runtime::findTerrain(protectedRecipe),
        "Protect polling rounded snapshot data or lost the published cache "
        "result");

  selectTerrain(workspace);
  authoring.draft()["seed"] = 73;
  const Json regeneratedRecipe = authoring.draft();
  check(authoring.generate(std::nullopt, error), "Regenerate failed: " + error);
  pollCompletion(workspace);
  check(terrain(workspace).recipe == regeneratedRecipe &&
            terrain(workspace).recipe["edits"] == protectedRecipe["edits"] &&
            terrain(workspace).recipe["regions"] == sculptedRecipe["regions"],
        "Regeneration discarded manual or biome edits");
  checkIndependentObject(workspace, building);
  check(workspace.save(error), "Terrain workspace Save failed: " + error);
  const Json saved = readJson(root / "scenes/main.scene.json");
  check(saved == workspace.sceneDocument().json() &&
            !workspace.sceneDocument().isDirty(),
        "Save did not persist the authored document");
  const auto text = saved.dump();
  check(saved["entities"].size() == 3 &&
            text.find("/__terrain/") == std::string::npos &&
            text.find("inline_geometry") == std::string::npos &&
            text.find("\"vertices\"") == std::string::npos &&
            text.find("\"generated\"") == std::string::npos,
        "Generated entities or geometry leaked into authored scene Save");
  const auto savedHeights = terrain(workspace).generated->heights;
  const auto savedField = terrain(workspace).generated;
  check(saved["entities"][0]["components"]["Terrain3D"]["recipe"]["edits"]
                .back() == protection,
        "Save changed noninteger protection snapshot data");
  editor::EditorWorkspace reopened;
  check(reopened.open(root, error),
        "Terrain workspace reload failed: " + error);
  check(terrain(reopened).recipe == regeneratedRecipe &&
            terrain(reopened).generated == savedField &&
            terrain(reopened).generated->heights == savedHeights &&
            reopened.project().world.entities.size() > saved["entities"].size(),
        "Reload did not reconstruct generated surface from authored recipe");
  checkIndependentObject(reopened, building);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    check(entry.path().filename() == "scenes" ||
              entry.path().filename() == "demi.project.json" ||
              entry.path().filename() == "main.scene.json",
          "Terrain workflow created a generated asset/cache in the source "
          "project");
}

void testCancellationAndStaleSelection(const std::filesystem::path &root) {
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(root, error),
        "Cancellation fixture open failed: " + error);
  selectTerrain(workspace);
  const Json before = workspace.sceneDocument().json();
  const auto field = terrain(workspace).generated;
  auto &authoring = workspace.terrainAuthoring();
  authoring.draft()["seed"] = 100;
  check(authoring.generate(std::nullopt, error),
        "Cancellation Generate failed: " + error);
  // busy remains true until polling, even if this small real worker
  // finishes quickly: cancellation before publication is deterministic
  // without sleeps.
  authoring.cancel();
  pollCompletion(workspace);
  check(workspace.sceneDocument().json() == before &&
            terrain(workspace).generated == field &&
            !workspace.sceneDocument().canUndo(),
        "Cancelled worker changed document, surface, or Undo history");

  selectTerrain(workspace);
  authoring.draft()["seed"] = 101;
  check(authoring.generate(std::nullopt, error),
        "Stale-selection Generate failed: " + error);
  workspace.selectEntity("building");
  pollCompletion(workspace);
  check(workspace.selectedEntityId() == "building" &&
            workspace.sceneDocument().json() == before &&
            terrain(workspace).generated == field &&
            !workspace.sceneDocument().canUndo(),
        "Worker for stale terrain selection committed into the workspace");

  // A real authored change during generation also invalidates the old
  // result.
  selectTerrain(workspace);
  authoring.draft()["seed"] = 102;
  check(authoring.generate(std::nullopt, error),
        "Stale-recipe Generate failed: " + error);
  auto newerRecipe = terrain(workspace).recipe;
  newerRecipe["seed"] = 103;
  check(
      workspace.editValue(
          {.entityId = "terrain", .component = "Terrain3D", .field = "recipe"},
          newerRecipe, false, error),
      "Concurrent authored edit failed: " + error);
  const auto newerField = terrain(workspace).generated;
  pollCompletion(workspace);
  check(terrain(workspace).recipe == newerRecipe &&
            terrain(workspace).generated == newerField,
        "Stale worker overwrote newer authored terrain");
  check(workspace.undo(error), "Concurrent edit Undo failed: " + error);
  check(workspace.sceneDocument().json() == before &&
            !workspace.sceneDocument().canUndo(),
        "Discarded stale result added an extra Undo entry");

  // After draining cancelled workers, the same workspace can generate
  // again.
  selectTerrain(workspace);
  authoring.draft()["seed"] = 104;
  check(authoring.generate(std::nullopt, error),
        "Generate after cancellation failed: " + error);
  pollCompletion(workspace);
  check(terrain(workspace).recipe["seed"] == 104,
        "Workspace did not recover after cancelled/stale generation");
}

void testLivePreviewSparseHistoryAndReadiness(
    const std::filesystem::path &root) {
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(root, error), "Live fixture open failed: " + error);
  selectTerrain(workspace);
  check(workspace.sceneView().alignToFirstCamera(workspace.project().world),
        "Live fixture camera could not align");
  auto &authoring = workspace.terrainAuthoring();
  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  authoring.brush.radius = .8F;
  authoring.brush.falloff = 0;
  const Json originalDocument = workspace.sceneDocument().json();
  const auto originalField = terrain(workspace).generated;
  const auto buildingRevision =
      runtime::findEntity(workspace.project().world, "building")
          ->component<runtime::MeshRendererComponent>()
          ->revision;
  std::string untouchedId;
  std::shared_ptr<const runtime::ColliderAsset3D> untouchedCollider;
  std::uint64_t untouchedRevision = 0;
  for (const auto &entity : workspace.project().world.entities) {
    if (runtime::terrainSurfaceOwner(entity) == "terrain" &&
        entity.id.find("/__terrain/4_4/") != std::string::npos) {
      untouchedId = entity.id;
      untouchedCollider =
          entity.component<runtime::ModelCollider3DComponent>()->inlineGeometry;
      untouchedRevision =
          entity.component<runtime::MeshRendererComponent>()->revision;
      break;
    }
  }
  check(!untouchedId.empty(), "Live fixture has no distant terrain chunk");
  const auto checkResources = [&] {
    const auto *untouched =
        runtime::findEntity(workspace.project().world, untouchedId);
    check(
        untouched &&
            untouched->component<runtime::ModelCollider3DComponent>()
                    ->inlineGeometry == untouchedCollider &&
            untouched->component<runtime::MeshRendererComponent>()->revision ==
                untouchedRevision,
        "Live publication/history rebuilt an untouched terrain resource");
    check(runtime::findEntity(workspace.project().world, "building")
                  ->component<runtime::MeshRendererComponent>()
                  ->revision == buildingRevision,
          "Live publication/history rebuilt an unrelated entity resource");
  };
  const auto pointer = [&](float x, float z, bool pressed) {
    const auto height = authoring.surface()->height({x, z});
    check(height.has_value(), "Live pointer missed local terrain");
    const auto position = editor::projectScenePoint3D(
        workspace.sceneView().camera(), {x, *height, z}, {800, 600});
    check(position.has_value(), "Live pointer could not project terrain");
    return editor::EditorViewportToolInput{.mousePosition = *position,
                                           .viewportSize = {800, 600},
                                           .hovered = true,
                                           .focused = true,
                                           .leftPressed = pressed,
                                           .leftDown = true};
  };
  // Interior points avoid shared-vertex rounding in camera/ray round trips.
  const auto firstPress = pointer(1.25F, 1.25F, true);
  check(editor::pickEditorTerrain(workspace.project().world, "terrain",
                                  authoring.surface(),
                                  workspace.sceneView().camera(), firstPress)
            .has_value(),
        "Projected live pointer did not hit the terrain surface");
  check(workspace.updateViewportTool(firstPress, error),
        "Live press failed: " + error);
  check(authoring.stroking(),
        "Projected live pointer did not start the selected brush");
  check(!workspace.terrainReady(error),
        "Play readiness allowed an active brush job");
  error.clear();
  check(!workspace.save(error), "Save allowed an uncommitted brush job");
  error.clear();
  check(!workspace.saveAll(error), "Save all allowed an uncommitted brush job");
  error.clear();
  check(readJson(root / "scenes/main.scene.json") == originalDocument,
        "Blocked Save changed the source document");
  pollCompletion(workspace);
  const auto firstPreview = terrain(workspace).generated;
  check(authoring.stroking() && firstPreview != originalField &&
            firstPreview->heights != originalField->heights,
        "First batch was not visible during the drag");
  check(workspace.sceneDocument().json() == originalDocument &&
            !workspace.sceneDocument().canUndo(),
        "Live preview created an authored command before release");
  checkResources();
  check(workspace.updateViewportTool(pointer(2.25F, 1.25F, false), error),
        "Live drag failed: " + error);
  pollCompletion(workspace);
  const auto secondPreview = terrain(workspace).generated;
  check(secondPreview != firstPreview &&
            secondPreview->heights != firstPreview->heights &&
            authoring.stroking() &&
            workspace.sceneDocument().json() == originalDocument,
        "Second batch did not publish during the same drag");
  check(!workspace.terrainReady(error),
        "Play readiness allowed preview-only terrain");
  error.clear();
  checkResources();
  // Release with a third batch pending after two live publications. The native
  // publication patch must start at the displayed field, while sparse history
  // still starts at the original stroke field.
  check(workspace.updateViewportTool(pointer(3.25F, 1.25F, false), error),
        "Third live batch failed: " + error);
  check(workspace.updateViewportTool({.focused = true, .leftReleased = true},
                                     error),
        "Live release failed: " + error);
  pollCompletion(workspace);
  const auto finalRecipe = terrain(workspace).recipe;
  const auto finalField = terrain(workspace).generated;
  check(workspace.terrainReady(error) && finalField != secondPreview &&
            finalField->heights != secondPreview->heights,
        "Release did not install the third pending batch or left readiness "
        "blocked");
  const auto *command = workspace.sceneDocument().nextTerrainUndo();
  check(command && command->samplePatch &&
            !command->samplePatch->samples.empty() &&
            !command->samplePatch->fullBefore &&
            !command->samplePatch->fullAfter,
        "Brush history did not retain a sparse native patch");
  check(workspace.undo(error), "Live stroke Undo failed: " + error);
  check(workspace.sceneDocument().json() == originalDocument &&
            terrain(workspace).generated->heights == originalField->heights &&
            !workspace.sceneDocument().canUndo(),
        "Multiple live batches did not coalesce to exactly one Undo command");
  checkResources();
  check(workspace.redo(error), "Live stroke Redo failed: " + error);
  check(terrain(workspace).recipe == finalRecipe &&
            terrain(workspace).generated->heights == finalField->heights,
        "Live stroke Redo did not restore the exact field/recipe");
  checkResources();
  const auto beforeCancel = terrain(workspace).generated;
  const auto beforeCancelDocument = workspace.sceneDocument().json();
  check(workspace.updateViewportTool(pointer(1.25F, 1.25F, true), error),
        "Cancellation press failed: " + error);
  pollCompletion(workspace);
  check(terrain(workspace).generated != beforeCancel,
        "Cancel fixture did not publish live terrain");
  check(workspace.updateViewportTool({.focused = true, .cancelPressed = true},
                                     error),
        "Live Escape failed: " + error);
  pollCompletion(workspace);
  check(workspace.terrainReady(error) &&
            terrain(workspace).generated == beforeCancel &&
            workspace.sceneDocument().json() == beforeCancelDocument,
        "Escape did not restore the exact live preview baseline");
  checkResources();
  check(workspace.undo(error) && !workspace.sceneDocument().canUndo(),
        "Cancelled live stroke left an additional Undo command");
  check(workspace.redo(error), "Redo before save failed: " + error);
  check(workspace.save(error), "Live recipe Save failed: " + error);
  check(readJson(root / "scenes/main.scene.json") ==
            workspace.sceneDocument().json(),
        "Live recipe Save did not persist the exact authored commit");
}

void testEmptyProceduralRecipeHistory() {
  TemporaryProject project;
  createProject(project.root);
  const auto path = project.root / "scenes/main.scene.json";
  auto source = readJson(path);
  const Json compactRecipe{{"format_version", 1}};
  source["entities"][0]["components"]["Terrain3D"]["recipe"] = compactRecipe;
  writeJson(path, source);
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(project.root, error),
        "Absent recipe open failed: " + error);
  selectTerrain(workspace);
  const auto before = terrain(workspace).generated;
  auto replacement = terrain(workspace).recipe;
  replacement["edits"].push_back(
      {{"type", "raise"}, {"center", {3, 3}}, {"radius", 1}});
  check(
      workspace.editValue(
          {.entityId = "terrain", .component = "Terrain3D", .field = "recipe"},
          replacement, false, error),
      "Absent recipe edit failed: " + error);
  const auto after = terrain(workspace).generated;
  check(workspace.undo(error), "Absent recipe Undo failed: " + error);
  check(
      workspace.sceneDocument().json() == source &&
          terrain(workspace).recipe == compactRecipe &&
          terrain(workspace).generated->heights == before->heights,
      "Absent recipe Undo did not restore default terrain and compact source");
  check(workspace.redo(error), "Absent recipe Redo failed: " + error);
  check(terrain(workspace).recipe == replacement &&
            terrain(workspace).generated->heights == after->heights,
        "Absent recipe Redo did not restore the exact brush result");
}

// A landscape preset is an ordinary DataAsset, so the picker, the loader and
// the undoable commit all run through the real project pipeline.
void testTerrainPresetApplyHistoryAndGridDecision() {
  TemporaryProject project;
  createProject(project.root);
  writePresetAsset(project.root, "alpine", alpinePreset("alpine", 4242));
  writePresetAsset(project.root, "rolling",
                   alpinePreset("rolling", 77, 8, false));
  writePresetAsset(project.root, "valley", alpinePreset("valley", 11, 16));
  {
    // Exists as a terrain_preset asset but the preset itself is invalid, so the
    // picker must report it instead of silently dropping it.
    auto broken = alpinePreset("broken", 5);
    broken.erase("name");
    writePresetAsset(project.root, "broken", broken);
  }
  editor::EditorWorkspace workspace;
  std::string error;
  const bool opened = workspace.open(project.root, error);
  check(opened, "Preset fixture open failed: " + error);
  selectTerrain(workspace);
  const Json initialDocument = workspace.sceneDocument().json();
  const Json initialRecipe = terrain(workspace).recipe;
  const auto initialField = terrain(workspace).generated;
  const Json building = *workspace.sceneDocument().entity("building");

  // Discovery comes from the asset registry, so the manifest's own id is what
  // the picker lists and what the loader resolves.
  const auto &presets = workspace.terrainAuthoring().presets();
  check(presets.size() == 3 + runtime::builtinTerrainPresets().size(),
        "Preset discovery did not list the three loadable presets");
  check(presets.front().id == "asset://terrain/presets/alpine" &&
            workspace.terrainAuthoring().preset(
                "asset://terrain/presets/valley") != nullptr,
        "Preset discovery did not use the manifest asset ids");
  check(workspace.terrainAuthoring().preset("asset://terrain/presets/rolling") !=
            nullptr,
        "Preset cache lost a listed preset");
  check(workspace.terrainPresetErrors().size() == 1 &&
            workspace.terrainPresetErrors().front().find("name") !=
                std::string::npos,
        "The invalid preset was not reported with the loader message");

  // Applying replaces generation keys only, and commits as one undoable recipe
  // edit, exactly like Generate.
  const bool applied =
      workspace.applyTerrainPreset("asset://terrain/presets/alpine", error);
  check(applied, "Preset apply failed: " + error);
  pollCompletion(workspace);
  const Json appliedRecipe = terrain(workspace).recipe;
  const auto appliedField = terrain(workspace).generated;
  check(appliedRecipe["seed"] == 4242 &&
            appliedRecipe["default_biome"] == "rock" &&
            appliedRecipe["resolution"] == Json::array({8, 8}),
        "Preset generation keys did not reach the recipe");
  check(appliedRecipe["preset_id"] == "asset://terrain/presets/alpine" &&
            appliedRecipe["preset_version"] == 1,
        "Preset apply did not record provenance");
  check(appliedRecipe["regions"] == initialRecipe["regions"] &&
            appliedRecipe["edits"] == initialRecipe["edits"],
        "Preset apply discarded authored regions or edits");
  check(appliedField != initialField &&
            appliedField->heights != initialField->heights,
        "Preset apply did not replace the visible native surface");
  checkIndependentObject(workspace, building);

  // Undo restores the exact previous document, draft and surface.
  const bool undone = workspace.undo(error);
  check(undone, "Preset apply Undo failed: " + error);
  check(workspace.sceneDocument().json() == initialDocument &&
            workspace.terrainAuthoring().draft() == initialRecipe &&
            terrain(workspace).generated->heights == initialField->heights,
        "Preset apply Undo did not restore the previous recipe and surface");
  const bool redone = workspace.redo(error);
  check(redone, "Preset apply Redo failed: " + error);
  check(terrain(workspace).recipe == appliedRecipe &&
            terrain(workspace).generated->heights == appliedField->heights,
        "Preset apply Redo did not restore the exact preset and surface");

  // Applying the same preset again is idempotent: no diff and no extra Undo
  // entry, so re-pressing Apply is not an edit an author has to undo.
  const bool reapplied =
      workspace.applyTerrainPreset("asset://terrain/presets/alpine", error);
  check(reapplied, "Repeated preset apply failed: " + error);
  check(terrain(workspace).recipe == appliedRecipe,
        "Repeated preset apply changed the committed recipe");
  const bool undoneAgain = workspace.undo(error);
  check(undoneAgain, "Repeated preset apply Undo failed: " + error);
  check(workspace.sceneDocument().json() == initialDocument &&
            !workspace.sceneDocument().canUndo(),
        "Repeated preset apply added a spurious Undo entry");
  const bool redoneAgain = workspace.redo(error);
  check(redoneAgain, "Could not restore the applied preset: " + error);
  check(terrain(workspace).recipe == appliedRecipe,
        "Redo did not restore the applied preset");

  // Clearing removes both provenance keys together and keeps the strokes.
  const bool cleared = workspace.clearTerrainPreset(error);
  check(cleared, "Preset clear failed: " + error);
  pollCompletion(workspace);
  check(!terrain(workspace).recipe.contains("preset_id") &&
            !terrain(workspace).recipe.contains("preset_version") &&
            terrain(workspace).recipe["regions"] == initialRecipe["regions"] &&
            terrain(workspace).recipe["edits"] == initialRecipe["edits"],
        "Preset clear left partial provenance or lost authored strokes");
  check(runtime::TerrainRecipe::parse(terrain(workspace).recipe).presetId.empty(),
        "A recipe without provenance did not parse");
  const bool clearUndone = workspace.undo(error);
  check(clearUndone, "Preset clear Undo failed: " + error);
  check(terrain(workspace).recipe == appliedRecipe,
        "Preset clear Undo did not restore the stamped recipe");
  const bool clearRedone = workspace.redo(error);
  check(clearRedone, "Preset clear Redo failed: " + error);
  check(!terrain(workspace).recipe.contains("preset_id"),
        "Preset clear Redo did not restore the cleared recipe");

  // A preset that cannot resolve the draft's own strokes is refused, and the
  // engine's own message reaches the author instead of being swallowed.
  const Json beforeRefusal = workspace.sceneDocument().json();
  const bool refused =
      workspace.applyTerrainPreset("asset://terrain/presets/rolling", error);
  check(!refused, "Merging a preset that drops a painted biome was accepted");
  check(error.find("unknown biome") != std::string::npos,
        "Preset refusal did not surface the merge message: " + error);
  check(workspace.sceneDocument().json() == beforeRefusal &&
            workspace.terrainAuthoring().draft() == terrain(workspace).recipe,
        "A refused preset apply mutated the document or its draft");
  error.clear();
  const bool missing =
      workspace.applyTerrainPreset("asset://terrain/presets/missing", error);
  check(!missing && error.find("not available") != std::string::npos,
        "An unknown preset id did not explain itself: " + error);
  error.clear();

  // A preset that moves the grid stops at the same decision a hand-edited size
  // does, so strokes are only resampled on an explicit choice.
  const Json documentBeforeResize = workspace.sceneDocument().json();
  const bool resizing =
      workspace.applyTerrainPreset("asset://terrain/presets/valley", error);
  check(!resizing, "A grid-changing preset should have stopped for a decision");
  check(error == "Choose how to preserve terrain edits after resizing.",
        "A grid-changing preset did not stop for the resize decision: " + error);
  check(workspace.terrainAuthoring().needsResizeDecision(),
        "A grid-changing preset did not open the resize decision");
  check(workspace.terrainAuthoring().draft()["edits"] == initialRecipe["edits"] &&
            workspace.terrainAuthoring().draft()["resolution"] ==
                Json::array({16, 16}),
        "The staged grid change did not keep strokes on the new grid");
  check(workspace.sceneDocument().json() == documentBeforeResize,
        "The decision was skipped and the document was written anyway");
  error.clear();
  const bool kept = workspace.terrainAuthoring().generate(
      editor::EditorTerrainResize::Keep, error);
  check(kept, "Keeping strokes across the preset grid failed: " + error);
  pollCompletion(workspace);
  check(terrain(workspace).recipe["edits"] == initialRecipe["edits"] &&
            terrain(workspace).recipe["preset_id"] ==
                "asset://terrain/presets/valley",
        "The grid-changing preset lost strokes or provenance");
  const bool resizeUndone = workspace.undo(error);
  check(resizeUndone, "The grid-changing preset Undo failed: " + error);
  check(workspace.sceneDocument().json() == documentBeforeResize,
        "The grid-changing preset Undo did not restore the previous document");
  workspace.terrainAuthoring().discardDraft();
  check(workspace.terrainAuthoring().draft() == terrain(workspace).recipe,
        "Discarding the draft did not restore the committed recipe");

  const bool saved = workspace.save(error);
  check(saved, "Preset workflow Save failed: " + error);
  checkIndependentObject(workspace, building);
  check(readJson(project.root / "scenes/main.scene.json") ==
            workspace.sceneDocument().json(),
        "Save did not persist the authored document");
}

void testLiveSelectionAndSourceConflict() {
  TemporaryProject project;
  createProject(project.root);
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(project.root, error),
        "Selection fixture open failed: " + error);
  selectTerrain(workspace);
  const auto baseline = workspace.sceneDocument().json();
  const auto baselineField = terrain(workspace).generated;
  auto &authoring = workspace.terrainAuthoring();
  const auto startStroke = [&] {
    authoring.brush.mode = editor::EditorTerrainBrush::Raise;
    authoring.brush.radius = 1;
    check(authoring.update({.hovered = true,
                            .focused = true,
                            .leftPressed = true,
                            .leftDown = true},
                           runtime::Vec3{1.1F, 0, 1.1F}, error),
          "Selection fixture stroke failed: " + error);
    pollCompletion(workspace);
    check(authoring.stroking() && terrain(workspace).generated != baselineField,
          "Selection fixture never published its transient field");
  };
  startStroke();
  workspace.selectEntity("building");
  pollCompletion(workspace);
  check(terrain(workspace).generated == baselineField &&
            workspace.sceneDocument().json() == baseline &&
            !workspace.sceneDocument().canUndo() &&
            workspace.terrainReady(error),
        "Selection change did not restore the transient field without history");
  selectTerrain(workspace);
  authoring.draft()["seed"] = 444;
  workspace.selectEntity("building");
  workspace.syncTerrainAuthoring();
  selectTerrain(workspace);
  check(authoring.draft()["seed"] == 444,
        "Selection cancellation lost a separate generation settings draft");
  authoring.discardDraft();
  startStroke();
  auto replacement =
      baseline["entities"][0]["components"]["Terrain3D"]["recipe"];
  replacement["seed"] = 145;
  check(
      workspace.editValue(
          {.entityId = "terrain", .component = "Terrain3D", .field = "recipe"},
          replacement, false, error),
      "Source replacement during preview failed: " + error);
  const auto replacementField = terrain(workspace).generated;
  pollCompletion(workspace);
  check(terrain(workspace).recipe == replacement &&
            terrain(workspace).generated == replacementField &&
            !authoring.stroking(),
        "Stale live preview overwrote a newer authored recipe");
  check(workspace.undo(error), "Source replacement Undo failed: " + error);
  check(workspace.sceneDocument().json() == baseline &&
            terrain(workspace).generated->heights == baselineField->heights &&
            !workspace.sceneDocument().canUndo(),
        "Cancelled preview leaked into authored source or history");
}

void testSceneSwitchPreservesTerrainDraft() {
  TemporaryProject project;
  createProject(project.root);
  const auto firstPath = project.root / "scenes/main.scene.json";
  const auto secondPath = project.root / "scenes/other.scene.json";
  auto secondSource = readJson(firstPath);
  secondSource["id"] = "scene://terrain_workspace/other";
  writeJson(secondPath, secondSource);
  auto projectSource = readJson(project.root / "demi.project.json");
  projectSource["scenes"].push_back({{"id", "scene://terrain_workspace/other"},
                                     {"path", "scenes/other.scene.json"}});
  writeJson(project.root / "demi.project.json", projectSource);

  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(project.root, error), error);
  selectTerrain(workspace);
  check(workspace.pinTerrainAuthoring("terrain", error), error);
  auto &authoring = workspace.terrainAuthoring();
  authoring.draft()["seed"] = 987;
  workspace.selectEntity("building");
  const auto draft = authoring.draft();
  const auto source = workspace.sceneDocument().json();
  const auto field = terrain(workspace).generated;
  check(!workspace.sceneDocument().isDirty(),
        "Terrain graph draft unexpectedly dirtied scene source");
  check(!workspace.openSceneDocument(secondPath, error) && !error.empty(),
        "Scene switch accepted an unapplied inline terrain graph draft");
  check(workspace.sceneDocument().path() == firstPath &&
            workspace.sceneDocument().json() == source &&
            authoring.draft() == draft && authoring.entityId() == "terrain" &&
            workspace.terrainAuthoringPinned() &&
            workspace.selectedEntityId() == "building" &&
            terrain(workspace).generated == field,
        "Refused scene switch changed graph ownership, selection or terrain "
        "data");

  error.clear();
  check(workspace.openSceneDocument(firstPath, error), error);
  check(authoring.draft() == draft && workspace.terrainAuthoringPinned(),
        "Re-focusing the current scene discarded its terrain draft");
  authoring.discardDraft();
  error.clear();
  check(workspace.openSceneDocument(secondPath, error), error);
  check(workspace.sceneDocument().path() == secondPath &&
            workspace.sceneDocument().json() == secondSource &&
            !workspace.terrainAuthoringPinned(),
        "Discarded draft still blocked scene switching or leaked its old pin");
}

void testPinnedTerrainTarget() {
  TemporaryProject project;
  createProject(project.root);
  // A second configured terrain proves retargeting cannot consume the first
  // graph's unapplied draft. It has independent authored and native identity.
  auto source = readJson(project.root / "scenes/main.scene.json");
  auto second = source["entities"][0];
  second["id"] = "other_terrain";
  second["components"]["Transform3D"]["position"] = {30, 0, 0};
  source["entities"].push_back(std::move(second));
  writeJson(project.root / "scenes/main.scene.json", source);
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(project.root, error), error);
  selectTerrain(workspace);
  check(workspace.pinTerrainAuthoring("terrain", error), error);
  auto &authoring = workspace.terrainAuthoring();
  authoring.brush.mode = editor::EditorTerrainBrush::Raise;
  check(authoring.brushActive(), "Selected pinned terrain disabled its brush");
  authoring.draft()["seed"] = 744;
  const auto draft = authoring.draft();
  workspace.selectEntity("building");
  check(
      workspace.terrainAuthoringPinned() && authoring.entityId() == "terrain" &&
          authoring.draft() == draft && !authoring.brushActive() &&
          workspace.selectedEntityId() == "building",
      "Inspector selection changed the pinned target, draft or brush routing");
  error.clear();
  check(!workspace.pinTerrainAuthoring("other_terrain", error) &&
            !error.empty() && authoring.entityId() == "terrain" &&
            authoring.draft() == draft,
        "Retargeting discarded an unapplied inline terrain draft");
  error.clear();
  check(!workspace.unpinTerrainAuthoring(error) && !error.empty() &&
            workspace.terrainAuthoringPinned() && authoring.draft() == draft,
        "Closing the graph discarded an unapplied inline terrain draft");
  error.clear();
  check(authoring.generate(std::nullopt, error), error);
  error.clear();
  check(!workspace.unpinTerrainAuthoring(error),
        "Closing released a graph with pending generation");
  error.clear();
  check(!workspace.pinTerrainAuthoring("other_terrain", error),
        "Retargeting released a graph with pending generation");
  pollCompletion(workspace);
  check(workspace.selectedEntityId() == "building" &&
            authoring.entityId() == "terrain" &&
            terrain(workspace).recipe.at("seed") == 744 &&
            !authoring.hasDraftChanges() && !authoring.brushActive(),
        "Graph generation did not commit to its owner independently of "
        "selection");
  check(workspace.sceneDocument().entity("building") != nullptr &&
            *workspace.sceneDocument().entity("building") ==
                source["entities"][1],
        "Pinned generation changed the inspected object's source");
  check(workspace.sceneDocument().entity("other_terrain") != nullptr &&
            *workspace.sceneDocument().entity("other_terrain") ==
                source["entities"].back(),
        "Pinned generation changed another terrain's source");
  check(workspace.undo(error), error);
  check(workspace.selectedEntityId() == "building" &&
            workspace.sceneDocument().json() == source,
        "Pinned generation Undo changed selection or missed its source owner");
  check(workspace.redo(error), error);
  check(terrain(workspace).recipe.at("seed") == 744,
        "Pinned generation Redo missed its source owner");
  selectTerrain(workspace);
  check(authoring.brushActive(),
        "Re-selecting the pinned terrain lost its brush mode");
  const auto appliedField = terrain(workspace).generated;
  authoring.brush.radius = 1;
  check(authoring.update({.hovered = true,
                          .focused = true,
                          .leftPressed = true,
                          .leftDown = true},
                         runtime::Vec3{1.1F, 0, 1.1F}, error),
        error);
  pollCompletion(workspace);
  check(authoring.stroking() && terrain(workspace).generated != appliedField,
        "Pinned stroke did not publish a transient preview");
  workspace.selectEntity("building");
  pollCompletion(workspace);
  check(
      workspace.terrainAuthoringPinned() && authoring.entityId() == "terrain" &&
          !authoring.stroking() && !authoring.brushActive() &&
          terrain(workspace).generated == appliedField,
      "Leaving pinned terrain did not cancel only its transient brush stroke");
  selectTerrain(workspace);
  workspace.selectEntity("other_terrain");
  check(authoring.entityId() == "terrain" && !authoring.brushActive(),
        "Another selected terrain redirected the pinned graph's brush");
  error.clear();
  check(workspace.unpinTerrainAuthoring(error), error);
  check(!workspace.terrainAuthoringPinned() &&
            authoring.entityId() == "other_terrain",
        "Unpin did not return authoring to the selected terrain");
  authoring.draft()["seed"] = 745;
  workspace.selectEntity("building");
  check(authoring.entityId().empty() && !authoring.brushActive(),
        "Unpinned non-terrain selection left brush authoring active");
  workspace.selectEntity("other_terrain");
  check(authoring.draft().at("seed") == 745,
        "Unpinned selection lost its retained settings draft");
}

void testInheritedPrefabRecipeHistory() {
  TemporaryProject project;
  createProject(project.root);
  const auto path = project.root / "scenes/main.scene.json";
  auto source = readJson(path);
  const auto terrainEntity = source["entities"][0];
  std::filesystem::create_directory(project.root / "prefabs");
  writeJson(project.root / "prefabs/ground.prefab.json",
            {{"format_version", 1},
             {"id", "prefab://ground"},
             {"entities", Json::array({terrainEntity})}});
  source["entities"].erase(source["entities"].begin());
  source["instances"] =
      Json::array({{{"id", "ground"}, {"prefab", "prefab://ground"}}});
  writeJson(path, source);
  editor::EditorWorkspace workspace;
  std::string error;
  check(workspace.open(project.root, error),
        "Inherited recipe open failed: " + error);
  workspace.setViewDimension(
      editor::EditorSceneViewDimension::ThreeDimensional);
  workspace.selectEntity("ground/terrain");
  workspace.syncTerrainAuthoring();
  const auto lookup = [&]() -> const runtime::Terrain3DComponent & {
    const auto *owner =
        runtime::findEntity(workspace.project().world, "ground/terrain");
    check(owner && owner->hasComponent<runtime::Terrain3DComponent>(),
          "Inherited terrain owner disappeared");
    return *owner->component<runtime::Terrain3DComponent>();
  };
  const auto beforeRecipe = lookup().recipe;
  const auto before = lookup().generated;
  const auto buildingRevision =
      runtime::findEntity(workspace.project().world, "building")
          ->component<runtime::MeshRendererComponent>()
          ->revision;
  auto replacement = beforeRecipe;
  replacement["edits"].push_back(
      {{"type", "raise"}, {"center", {2.2F, 1.1F}}, {"radius", 1}});
  check(workspace.editValue({.entityId = "ground/terrain",
                             .component = "Terrain3D",
                             .field = "recipe"},
                            replacement, false, error),
        "Inherited recipe edit failed: " + error);
  const auto after = lookup().generated;
  check(workspace.undo(error), "Inherited recipe Undo failed: " + error);
  check(workspace.sceneDocument().json() == source &&
            lookup().recipe == beforeRecipe &&
            lookup().generated->heights == before->heights,
        "Inherited recipe Undo did not resolve the source after removing the "
        "override");
  check(workspace.redo(error), "Inherited recipe Redo failed: " + error);
  check(lookup().recipe == replacement &&
            lookup().generated->heights == after->heights,
        "Inherited recipe Redo did not restore the exact override and surface");
  check(runtime::findEntity(workspace.project().world, "building")
                ->component<runtime::MeshRendererComponent>()
                ->revision == buildingRevision,
        "Inherited terrain history rebuilt unrelated scene resources");
}
} // namespace

int main() {
  try {
    TemporaryProject project;
    createProject(project.root);
    testGenerateRegenerateHistoryAndSave(project.root);
    testCancellationAndStaleSelection(project.root);
    TemporaryProject liveProject;
    createProject(liveProject.root);
    testLivePreviewSparseHistoryAndReadiness(liveProject.root);
    testEmptyProceduralRecipeHistory();
    testTerrainPresetApplyHistoryAndGridDecision();
    testInheritedPrefabRecipeHistory();
    testLiveSelectionAndSourceConflict();
    testPinnedTerrainTarget();
    testSceneSwitchPreservesTerrainDraft();
    std::cout << "Editor terrain workspace tests passed.\n";
  } catch (const std::exception &exception) {
    std::cerr << "Editor terrain workspace tests failed: " << exception.what()
              << '\n';
    return 1;
  }
  return 0;
}
