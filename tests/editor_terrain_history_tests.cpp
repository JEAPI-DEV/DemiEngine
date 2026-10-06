#include "demi/runtime/terrain/TerrainUpdate.h"
#include "editor/EditorSceneDocument.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace demi;
using Json = nlohmann::json;

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const std::filesystem::path &path, const Json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  output.close();
  check(!output.fail(), "Could not write terrain history fixture");
}

struct TemporaryProject {
  std::filesystem::path root;

  TemporaryProject() {
    const auto pattern = (std::filesystem::temp_directory_path() /
                          "demi-editor-terrain-history-XXXXXX")
                             .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    check(created != nullptr, "Could not create terrain history fixture");
    root = created;
    writeJson(root / "demi.project.json",
              {{"format_version", 1},
               {"name", "Terrain history"},
               {"main_scene", "scene://history/main"},
               {"scenes",
                {{{"id", "scene://history/main"},
                  {"path", "scenes/main.scene.json"}}}}});
  }

  ~TemporaryProject() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean terrain history fixture: "
                << error.message() << '\n';
  }

  TemporaryProject(const TemporaryProject &) = delete;
  TemporaryProject &operator=(const TemporaryProject &) = delete;
};

const editor::SceneValueTarget terrainTarget{
    .entityId = "terrain", .component = "Terrain3D", .field = "recipe"};

Json recipe(bool protectedSamples = false) {
  runtime::TerrainRecipe native;
  native.size = {32, 32};
  native.cellsX = native.cellsZ = 32;
  native.chunkCells = 8;
  native.landforms.at("default").baseHeight = 2;
  native.landforms.at("default").heightVariation = 0;
  if (protectedSamples) {
    runtime::TerrainEdit protection;
    protection.kind = runtime::TerrainEditKind::Protect;
    protection.center = {15.5F, 15.5F};
    protection.radius = 12;
    protection.falloff = 0;
    protection.snapshotSize = native.size;
    protection.snapshotCellsX = native.cellsX;
    protection.snapshotCellsZ = native.cellsZ;
    for (int z = 8; z < 24; ++z)
      for (int x = 8; x < 24; ++x)
        protection.samples.push_back({{float(x), float(z)}, 2});
    native.edits.push_back(std::move(protection));
  }
  Json result = native.toJson();
  if (protectedSamples)
    result["edits"][0]["snapshot"]["samples"][0]["height"] = 2.123456789123;
  return result;
}

Json raised(Json before, double x, double amount) {
  before["edits"].push_back({{"type", "raise"},
                             {"center", {x, 3.123456789123}},
                             {"radius", 2.123456789123},
                             {"amount", amount}});
  return before;
}

Json scene(const Json &terrainRecipe) {
  return {{"format_version", 1},
          {"id", "scene://history/main"},
          {"entities",
           {{{"id", "terrain"},
             {"components",
              {{"Transform3D", Json::object()},
               {"Terrain3D", {{"recipe", terrainRecipe}}}}}},
            {{"id", "other"},
             {"components",
              {{"GameplayData",
                {{"values", {{"unrelated", std::string(4096, 'x')}}}}}}}}}}};
}

editor::EditorSceneDocument openDocument(const TemporaryProject &project,
                                         const Json &source) {
  const auto path = project.root / "scenes/main.scene.json";
  writeJson(path, source);
  editor::EditorSceneDocument document;
  std::string error;
  check(document.open(path, error), error);
  return document;
}

std::shared_ptr<const runtime::HeightField> generate(const Json &source) {
  auto field = runtime::TerrainGenerator::generate(
      runtime::TerrainRecipe::parse(source));
  check(field.has_value(), "Terrain fixture generation failed");
  return std::make_shared<const runtime::HeightField>(std::move(*field));
}

runtime::TerrainUpdate
localUpdate(const Json &before, const Json &after,
            std::shared_ptr<const runtime::HeightField> current) {
  auto update = runtime::updateTerrain(runtime::TerrainRecipe::parse(before),
                                       runtime::TerrainRecipe::parse(after),
                                       std::move(current));
  check(update && update->field && update->patch,
        "Local terrain update failed");
  check(!update->patch->samples.empty(),
        "Brush fixture did not change samples");
  check(!update->patch->fullBefore && !update->patch->fullAfter,
        "Local brush patch retained full terrain fields");
  return std::move(*update);
}

void checkHeights(const std::shared_ptr<const runtime::HeightField> &actual,
                  const std::shared_ptr<const runtime::HeightField> &expected) {
  check(actual && expected &&
            actual->heights.size() == expected->heights.size(),
        "Terrain history field dimensions differ");
  for (std::size_t index = 0; index < expected->heights.size(); ++index) {
    check(actual->heights[index] == expected->heights[index],
          "Terrain history did not restore an exact height");
    const auto &left = actual->normals[index];
    const auto &right = expected->normals[index];
    check(left.x == right.x && left.y == right.y && left.z == right.z,
          "Terrain history did not restore an exact normal");
  }
}

void sparseStrokeHistory() {
  TemporaryProject project;
  const auto before = recipe(true);
  const auto first = raised(before, 3.123456789123, 0.123456789123);
  const auto second = raised(first, 4.123456789123, 0.234567891234);
  const auto field = generate(before);
  const auto firstUpdate = localUpdate(before, first, field);
  const auto secondUpdate = localUpdate(first, second, firstUpdate.field);
  auto document = openDocument(project, scene(before));
  const auto original = document.json();
  std::string error;

  check(document.nextTerrainUndo() == nullptr &&
            document.nextTerrainRedo() == nullptr,
        "Empty history returned a terrain command");
  check(document.setTerrainRecipe(terrainTarget, first, firstUpdate.patch, true,
                                  error),
        error);
  const auto *command = document.nextTerrainUndo();
  check(command && command->samplePatch == firstUpdate.patch,
        "History did not share the supplied sparse patch");
  check(command->forwardPatch == Json::diff(before, first) &&
            command->inversePatch == Json::diff(first, before),
        "History deltas are not relative to the recipe");
  check(command->forwardPatch.dump().size() +
                command->inversePatch.dump().size() <
            before.dump().size() / 10,
        "Small stroke retained the unchanged protection sample array");

  const auto firstCommitted = document.json();
  auto incompatible =
      std::make_shared<runtime::TerrainPatch>(*secondUpdate.patch);
  ++incompatible->cellsX;
  check(!document.setTerrainRecipe(terrainTarget, second, incompatible, true,
                                   error),
        "Incompatible terrain patches were coalesced");
  check(document.json() == firstCommitted &&
            document.nextTerrainUndo() == command &&
            command->samplePatch == firstUpdate.patch && !document.canRedo(),
        "Failed patch merge changed source or history");

  check(document.setTerrainRecipe(terrainTarget, second, secondUpdate.patch,
                                  true, error),
        error);
  command = document.nextTerrainUndo();
  check(command && command->forwardPatch == Json::diff(before, second) &&
            command->inversePatch == Json::diff(second, before),
        "Continuous updates lost the pre-stroke recipe");
  check(command->samplePatch && !command->samplePatch->fullBefore &&
            !command->samplePatch->fullAfter,
        "Coalescing retained a whole terrain field");
  check(command->samplePatch->samples.size() <=
            firstUpdate.patch->samples.size() +
                secondUpdate.patch->samples.size(),
        "Coalescing duplicated overlapping sample history");
  const auto merged = command->samplePatch;
  checkHeights(
      runtime::applyTerrainPatch(secondUpdate.field, *merged, false).field,
      field);
  checkHeights(runtime::applyTerrainPatch(field, *merged, true).field,
               secondUpdate.field);

  document.endContinuousEdit();
  check(document.undo(error), error);
  check(document.json() == original && !document.canUndo() &&
            !document.isDirty(),
        "One undo did not restore the entire stroke exactly");
  check(document.lastChangedEntityId() == "terrain" &&
            document.nextTerrainRedo() &&
            document.nextTerrainRedo()->samplePatch == merged,
        "Terrain redo did not expose the moved command");
  check(document.redo(error), error);
  check(*editor::valueInDocument(document.json(), terrainTarget) == second,
        "Redo rounded the recipe or protected sample payload");

  check(document.setValue({.entityId = "other", .field = "name"}, "Renamed",
                          false, error),
        error);
  check(document.nextTerrainUndo() == nullptr,
        "Terrain query skipped a non-terrain command");
  check(document.undo(error), error);
  check(document.nextTerrainUndo() && document.nextTerrainRedo() == nullptr,
        "Terrain query did not inspect only the next history entry");
}

void atomicFailureAndCancellation() {
  TemporaryProject project;
  const auto before = recipe();
  const auto first = raised(before, 3, 1);
  const auto second = raised(first, 4, 1);
  auto document = openDocument(project, scene(before));
  std::string error;
  check(document.setTerrainRecipe(terrainTarget, first, {}, false, error),
        error);
  check(document.undo(error), error);
  const auto original = document.json();
  const auto redoPatch = document.nextTerrainRedo()->forwardPatch;
  check(document.setTerrainRecipe(terrainTarget, second, {}, true, error),
        error);
  const auto committed = document.json();
  const auto *history = document.nextTerrainUndo();
  const auto inverse = history->inversePatch;

  auto invalid = second;
  invalid["resolution"] = {0, 32};
  check(!document.setTerrainRecipe(terrainTarget, invalid, {}, true, error),
        "Invalid terrain recipe was accepted");
  check(!error.empty() && document.issueFor(terrainTarget) &&
            document.json() == committed &&
            document.nextTerrainUndo() == history &&
            history->inversePatch == inverse && !document.canRedo(),
        "Validation failure mutated source or history");
  check(
      !document.setTerrainRecipe(
          {.entityId = "missing", .component = "Terrain3D", .field = "recipe"},
          first, {}, false, error),
      "Missing terrain target was accepted");
  check(document.json() == committed && document.nextTerrainUndo() == history,
        "Missing target mutated source or history");
  check(!document.setTerrainRecipe({.entityId = "terrain",
                                    .component = "Transform3D",
                                    .field = "position"},
                                   {1, 2, 3}, {}, false, error),
        "Terrain history accepted a different field");

  check(document.cancelContinuousEdit(error), error);
  check(document.json() == original && !document.canUndo() &&
            document.nextTerrainRedo() &&
            document.nextTerrainRedo()->forwardPatch == redoPatch,
        "Cancelled stroke did not restore source and the original redo branch");
  check(document.redo(error), error);
  check(*editor::valueInDocument(document.json(), terrainTarget) == first,
        "Cancellation replaced the existing redo command");

  check(document.setTerrainRecipe(terrainTarget, first, {}, false, error),
        error);
  check(document.undo(error) && !document.canUndo(),
        "Unchanged recipe created a history command");
  check(document.setTerrainRecipe(terrainTarget, second, {}, true, error),
        error);
  check(document.setTerrainRecipe(terrainTarget, second, {}, false, error),
        error);
  check(document.cancelContinuousEdit(error), error);
  check(*editor::valueInDocument(document.json(), terrainTarget) == second,
        "Unchanged final update did not close the continuous stroke");

  document.endContinuousEdit();
  check(document.setTerrainRecipe(terrainTarget, first, {}, true, error),
        error);
  check(document.undo(error), error);
  check(*editor::valueInDocument(document.json(), terrainTarget) == second &&
            document.canUndo(),
        "Separate strokes coalesced across an explicit stroke boundary");
}

void omittedRecipeAndGenericChoice() {
  TemporaryProject project;
  auto source = scene(recipe());
  source["entities"][0]["components"]["Terrain3D"] = Json::object();
  auto document = openDocument(project, source);
  std::string error;
  check(document.setTerrainRecipe(terrainTarget, recipe(), {}, false, error),
        error);
  check(document.undo(error) && document.json() == source,
        "Undo materialized an omitted recipe default");
  check(document.redo(error), error);
  check(document.setValue(terrainTarget, raised(recipe(), 3, 1), false, error),
        error);
  check(document.nextTerrainUndo() == nullptr,
        "Generic setValue was silently converted to terrain history");
  check(document.undo(error) && document.nextTerrainUndo(),
        "Generic terrain edit lost the sparse history entry below it");
}

void generationHistory() {
  TemporaryProject project;
  const auto before = recipe();
  const auto first = raised(before, 3, 1);
  const auto field = generate(before);
  const auto brush = localUpdate(before, first, field);
  auto regenerated = first;
  regenerated["seed"] = 42;
  const auto update = runtime::regenerateTerrain(
      runtime::TerrainRecipe::parse(regenerated), brush.field, "Seed changed");
  check(update && update->patch && update->patch->fullBefore == brush.field &&
            update->patch->fullAfter == update->field,
        "Explicit regeneration did not retain its global patch");
  auto document = openDocument(project, scene(first));
  std::string error;
  check(document.setTerrainRecipe(terrainTarget, regenerated, update->patch,
                                  false, error),
        error);
  check(document.nextTerrainUndo()->samplePatch == update->patch,
        "History dropped an explicit global patch");
  check(document.undo(error) &&
            *editor::valueInDocument(document.json(), terrainTarget) == first,
        "Global recipe undo did not restore the previous authored recipe");

  auto localDocument = openDocument(project, scene(before));
  check(localDocument.setTerrainRecipe(terrainTarget, first, brush.patch, true,
                                       error),
        error);
  check(localDocument.setTerrainRecipe(terrainTarget, regenerated, {}, true,
                                       error),
        error);
  check(localDocument.nextTerrainUndo() &&
            !localDocument.nextTerrainUndo()->samplePatch,
        "History exposed a partial sample patch for a stroke containing "
        "regeneration");
  localDocument.endContinuousEdit();
  check(localDocument.undo(error) &&
            *editor::valueInDocument(localDocument.json(), terrainTarget) ==
                before &&
            !localDocument.canUndo(),
        "Recipe-only fallback did not undo the whole continuous edit");
}

void prefabSourceShapes() {
  TemporaryProject project;
  const auto before = recipe(true);
  const auto first = raised(before, 3, 0.123456789123);
  const auto second = raised(first, 4, 0.234567891234);
  writeJson(project.root / "prefabs/terrain.prefab.json",
            {{"format_version", 1},
             {"id", "prefab://terrain"},
             {"entities", scene(before).at("entities")}});
  const editor::SceneValueTarget target{.entityId = "instance/terrain",
                                        .component = "Terrain3D",
                                        .field = "recipe",
                                        .prefabInstanceId = "instance",
                                        .prefabEntityId = "terrain"};
  const std::string key = "terrain.Terrain3D.recipe";
  const Json nested = {
      {"terrain", {{"components", {{"Terrain3D", {{"recipe", before}}}}}}}};
  const Json emptyComponent = {
      {"terrain", {{"components", {{"Terrain3D", Json::object()}}}}}};
  const Json unrelated = {{"terrain.Transform3D.position", {1, 2, 3}}};
  auto shadowed = nested;
  shadowed[key] = before;
  shadowed["terrain"]["components"]["Terrain3D"]["recipe"]["seed"] = 42;
  const std::vector<std::optional<Json>> shapes{
      std::nullopt,        Json::object(), emptyComponent, unrelated,
      Json{{key, before}}, nested,         shadowed};

  for (const bool inlineInstance : {true, false}) {
    for (const auto &overrides : shapes) {
      auto source = scene(before);
      Json instance = {{"id", "instance"}, {"prefab", "prefab://terrain"}};
      if (overrides)
        instance["overrides"] = *overrides;
      source[inlineInstance ? "entities" : "instances"] =
          Json::array({instance});
      if (!inlineInstance)
        source["entities"] = Json::array();
      auto document = openDocument(project, source);
      std::string error;
      check(document.setTerrainRecipe(target, first, {}, true, error), error);
      check(document.setTerrainRecipe(target, second, {}, true, error), error);
      const auto committed = document.json();
      const auto *command = document.nextTerrainUndo();
      check(command && command->prefabSourceInversePatch.dump().size() < 512,
            "Prefab history retained recipe samples in source-shape metadata");
      check(!command->prefabNestedRecipePatch ||
                command->prefabNestedRecipePatch->dump().size() < 128,
            "Prefab history retained the complete nested recipe");
      document.endContinuousEdit();
      check(document.undo(error) && document.json() == source &&
                !document.canUndo(),
            "Prefab undo did not restore the exact source shape");
      check(document.redo(error) && document.json() == committed,
            "Prefab redo did not reproduce the exact committed shape");
      check(document.save(error), error);
      check(document.reload(error) && document.json() == committed,
            "Prefab terrain recipe changed across save/reload");
    }
  }
}
} // namespace

int main() {
  try {
    sparseStrokeHistory();
    atomicFailureAndCancellation();
    omittedRecipeAndGenericChoice();
    generationHistory();
    prefabSourceShapes();
    std::cout << "Terrain history tests passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "Terrain history tests failed: " << exception.what() << '\n';
    return 1;
  }
}
