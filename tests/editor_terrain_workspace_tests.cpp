#include "editor/EditorTerrainPicking.h"
#include "editor/EditorScenePreview.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <chrono>
#include <cmath>
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

void createProject(const std::filesystem::path &root) {
  runtime::TerrainRecipe recipe;
  recipe.size = {8.8F, 8.8F};
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.chunkCells = 4;
  recipe.biomes.at("default").featureSize = 3;
  recipe.biomes.at("default").heightVariation = 1;
  recipe.biomes.emplace("hill", runtime::TerrainBiome{.baseHeight = 4,
                                                      .heightVariation = 1,
                                                      .featureSize = 3});
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
    if (component)
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
      check(editor::editorPlacementOwner(workspace.project().world, candidate.id) == "terrain",
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
} // namespace

int main() {
  try {
    TemporaryProject project;
    createProject(project.root);
    testGenerateRegenerateHistoryAndSave(project.root);
    testCancellationAndStaleSelection(project.root);
    std::cout << "Editor terrain workspace tests passed.\n";
  } catch (const std::exception &exception) {
    std::cerr << "Editor terrain workspace tests failed: " << exception.what()
              << '\n';
    return 1;
  }
  return 0;
}
