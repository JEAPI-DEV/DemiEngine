#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainAssetPayloadData.h"
#include "demi/assets/TerrainSurfaceReferences.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainUpdate.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi;
using namespace demi::assets;
using namespace demi::runtime;

namespace {

std::filesystem::path temporaryRoot() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("demi-terrain-asset-test-" + std::to_string(nonce));
  std::filesystem::create_directories(path);
  return path;
}

TerrainRecipe smallRecipe() {
  TerrainRecipe recipe;
  recipe.size = {16, 12};
  recipe.cellsX = 8;
  recipe.cellsZ = 6;
  recipe.chunkCells = 4;
  recipe.biomes.emplace("rock", TerrainBiome{.color = {.5F, .4F, .3F, 1}});
  return recipe;
}

void writeText(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  assert(output);
  output << text;
  assert(output);
}

AssetRegistry registryFor(const std::filesystem::path &root,
                          const TerrainRecipe &recipe) {
  AssetRegistry registry;
  registry.projectDirectory = root / "project";
  const auto source =
      registry.projectDirectory / "assets" / "hill.terrain.json";
  writeText(source, terrainAssetSourceJson({.id = "asset://terrain/hill",
                                            .name = "Hill",
                                            .recipe = recipe.toJson()})
                        .dump(2));
  AssetManifest manifest;
  manifest.id = "asset://terrain/hill";
  manifest.type = "Terrain";
  manifest.importer = "terrain_heightfield";
  manifest.sourcePath = source;
  manifest.sourcePaths = {source};
  registry.assets.push_back(std::move(manifest));
  return registry;
}

void expectFailure(const auto &operation, std::string_view fragment) {
  try {
    operation();
    assert(false && "Expected an actionable failure");
  } catch (const std::exception &error) {
    assert(std::string_view(error.what()).find(fragment) !=
           std::string_view::npos);
  }
}

void sourceAndPersistentCache(const std::filesystem::path &root) {
  const auto recipe = smallRecipe();
  auto registry = registryFor(root, recipe);
  const auto id = std::string("asset://terrain/hill");
  expectFailure([&] { (void)loadTerrainAsset(registry, id); }, "prepare");
  auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  const auto prepared = storeTerrainAssetPreview(
      registry, id, recipe.toJson(),
      std::make_shared<const HeightField>(std::move(*generated)));
  assert(std::filesystem::is_regular_file(prepared));
  assert(prepared.string().find("/assets/") == std::string::npos);
  assert(prepareTerrainAsset(registry, id) == prepared);

  nlohmann::json editable;
  const auto first = loadTerrainAsset(registry, id, &editable);
  const auto second = loadTerrainAsset(registry, id);
  assert(first == second);
  assert(editable == recipe.toJson());
  assert(first->heights.size() == recipe.sampleCount());
  const auto inputs = resolveTerrainAssetGenerationInputs(registry, recipe);
  assert(inputs.fingerprint == first->inputFingerprint);
  auto sculpted = recipe;
  sculpted.edits.push_back({.kind = TerrainEditKind::Raise,
                            .center = {4, 3},
                            .radius = 2,
                            .amount = 1});
  const auto updated = updateTerrainWithInputs(recipe, sculpted, first, inputs);
  assert(updated);
  assert(!updated->invalidation.fullGeneration);
  assert(updated->stats.baseEvaluations == 0);

  auto shipped = registry;
  shipped.assets.front().sourcePath = prepared;
  shipped.assets.front().sourcePaths = {prepared};
  const auto fromBinary = loadTerrainAsset(shipped, id, &editable);
  assert(fromBinary == first);
  assert(editable.is_null());
  assert(prepareTerrainAsset(shipped, id) == prepared);

  const auto originalHash = hashFile(prepared);
  assert(originalHash);
  auto badField = std::make_shared<HeightField>(*first);
  badField->heights.clear();
  expectFailure(
      [&] {
        (void)storeTerrainAssetPreview(registry, id, recipe.toJson(), badField);
      },
      "sample");
  assert(hashFile(prepared) == originalHash);

  auto editedRecipe = recipe;
  editedRecipe.seed += 1;
  writeText(registry.assets.front().sourcePath,
            terrainAssetSourceJson({.id = id, .recipe = editedRecipe.toJson()})
                .dump(2));
  expectFailure([&] { (void)loadTerrainAsset(registry, id); }, "stale");
  assert(hashFile(prepared) == originalHash);
  const auto regeneratedPath = prepareTerrainAsset(registry, id);
  assert(regeneratedPath != prepared);
  assert(hashFile(prepared) == originalHash);
  assert(std::filesystem::is_regular_file(regeneratedPath));
  assert(loadTerrainAsset(registry, id)->heights.size() ==
         recipe.sampleCount());
}

void completePayloadRoundTrip() {
  const auto recipe = smallRecipe();
  auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  auto field = std::move(*generated);
  field.stageOrder = "graph,surface";
  field.paletteId = "asset://palette/test";
  field.inputFingerprint = "fingerprint";
  auto palette = std::make_shared<TerrainPalette>();
  palette->id = field.paletteId;
  palette->name = "Test palette";
  palette->materialSet = "asset://surface_set";
  TerrainPaletteEntry ruleId;
  ruleId.model = "asset://tree";
  palette->placements.emplace("tree", ruleId);
  field.resolvedPalette = palette;
  TerrainScatterPlacement tree;
  tree.ruleId = "tree";
  tree.model = ruleId.model;
  tree.biome = 0;
  tree.biomeName = field.biomeIds[0];
  tree.cell = 1;
  tree.position = {2, field.height(1, 0), 0};
  field.scatterPlacements.push_back(tree);

  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  artifacts->baseField = std::make_shared<const HeightField>(field);
  artifacts->basePlacements =
      std::make_shared<const std::vector<TerrainScatterPlacement>>(
          std::vector{tree});
  artifacts->water.authored = true;
  artifacts->water.bodies.push_back({.id = "pond",
                                     .kind = TerrainWaterBody::Lake,
                                     .level = 2,
                                     .center = {5, 5},
                                     .radius = 3});
  TerrainWaterResult water;
  water.carvedHeights = field.heights;
  TerrainWaterSurface surface;
  surface.id = "pond";
  surface.kind = TerrainWaterBody::Lake;
  surface.level = 2;
  surface.vertices = {{0, 2, 0}, {1, 2, 0}, {1, 2, 1}, {0, 2, 1}};
  surface.normals = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
  surface.indices = {0, 1, 2, 0, 2, 3};
  surface.depth = {1, 1, 1, 1};
  water.surfaces.push_back(std::move(surface));
  artifacts->waterResult = std::move(water);
  field.graphArtifacts = std::move(artifacts);

  const auto derived = terrain_payload::encodeDerived(field);
  assert(derived["graph"]["water_result"]["resolved_coverage"].is_null());
  auto payload = serializeTerrainAssetPayload(field, "recipe", "fingerprint");
  const auto restored = deserializeTerrainAssetPayload(payload);
  assert(restored->heights == field.heights);
  assert(restored->baseHeights == field.baseHeights);
  assert(restored->normals.size() == field.normals.size());
  for (std::size_t index = 0; index < field.normals.size(); ++index) {
    assert(restored->normals[index].x == field.normals[index].x);
    assert(restored->normals[index].y == field.normals[index].y);
    assert(restored->normals[index].z == field.normals[index].z);
  }
  assert(restored->biomeIndices == field.biomeIndices);
  assert(restored->chunks.size() == field.chunks.size());
  assert(restored->scatterPlacements.size() == 1);
  assert(restored->resolvedPalette &&
         restored->resolvedPalette->placements.size() == 1);
  assert(restored->resolvedPalette->materialSet == "asset://surface_set");
  assert(restored->graphArtifacts && restored->graphArtifacts->baseField);
  assert(restored->graphArtifacts->basePlacements->size() == 1);
  assert(restored->graphArtifacts->water.bodies.size() == 1);
  assert(restored->graphArtifacts->waterResult->surfaces.size() == 1);
  assert(restored->graphArtifacts->waterResult->carvedHeights == field.heights);
  assert(!restored->graphArtifacts->waterResult->resolvedCoverage);
  payload.back() ^= std::byte{1};
  expectFailure([&] { (void)deserializeTerrainAssetPayload(payload); },
                "checksum");
}

HeightField fieldWithWaterCoverage() {
  auto generated = TerrainGenerator::generate(smallRecipe());
  assert(generated);
  auto field = std::move(*generated);
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  artifacts->baseField = std::make_shared<const HeightField>(field);
  artifacts->water.authored = true;
  artifacts->water.bodies = {
      {.id = "pond", .kind = TerrainWaterBody::Lake, .level = 2},
      {.id = "rejected", .kind = TerrainWaterBody::River, .level = 7},
      {.id = "ocean", .kind = TerrainWaterBody::Ocean, .level = 4}};
  TerrainWaterResult water;
  water.carvedHeights = field.heights;
  water.carvedHeights.set(0, 1);
  water.carvedHeights.set(1, 5);
  water.carvedHeights.set(2, 1);
  water.dropped = 1;
  auto coverage = std::make_shared<terrain_water_detail::WaterLevelField>();
  coverage->size = field.size;
  coverage->cellsX = field.cellsX;
  coverage->cellsZ = field.cellsZ;
  coverage->level.assign(field.heights.size(), 0.F);
  coverage->wet.assign(field.heights.size(), 0);
  coverage->body.assign(field.heights.size(),
                        terrain_water_detail::WaterLevelField::noBody);
  coverage->level[0] = 2;
  coverage->wet[0] = 1;
  coverage->body[0] = 0;
  // An owned shore sample is ground-dry. The next sample stays unowned despite
  // lying below both authored levels: connectivity is already resolved.
  coverage->level[1] = 4;
  coverage->wet[1] = 1;
  coverage->body[1] = 2;
  water.resolvedCoverage = std::move(coverage);
  artifacts->waterResult = std::move(water);
  field.graphArtifacts = std::move(artifacts);
  return field;
}

void waterCoverageRoundTrip() {
  const auto field = fieldWithWaterCoverage();
  const auto derived = terrain_payload::encodeDerived(field);
  const auto &encoded = derived["graph"]["water_result"]["resolved_coverage"];
  assert(encoded.is_object() && encoded.size() == 1);
  assert(encoded["body"].size() == field.heights.size());
  assert(encoded["body"][0] == 0 && encoded["body"][1] == 2 &&
         encoded["body"][2] == -1);
  const auto payload = serializeTerrainAssetPayload(field, "recipe", "inputs");
  assert(terrainAssetPayloadVersion == 5 && payload[7] == std::byte{'5'});
  const auto restored = deserializeTerrainAssetPayload(payload);
  const auto &before = *field.graphArtifacts->waterResult;
  const auto &after = *restored->graphArtifacts->waterResult;
  assert(after.resolvedCoverage);
  const auto &coverage = *after.resolvedCoverage;
  assert(coverage.size.x == field.size.x && coverage.size.y == field.size.y);
  assert(coverage.cellsX == field.cellsX && coverage.cellsZ == field.cellsZ);
  assert(coverage.body == before.resolvedCoverage->body);
  assert(coverage.wet == before.resolvedCoverage->wet);
  assert(coverage.level == before.resolvedCoverage->level);
  assert(after.carvedHeights == before.carvedHeights && after.dropped == 1);
  assert(after.carvedHeights[1] > coverage.level[1] && coverage.wet[1] == 1);
  assert(after.carvedHeights[2] < 2 && coverage.wet[2] == 0);

  auto oldPayload = payload;
  oldPayload[7] = std::byte{'3'};
  expectFailure([&] { (void)deserializeTerrainAssetPayload(oldPayload); },
                "unknown format");
}

void malformedWaterCoverageDecode() {
  const auto field = fieldWithWaterCoverage();
  const auto derived = terrain_payload::encodeDerived(field);
  const auto reject = [&](const auto &mutate, std::string_view fragment) {
    auto document = derived;
    mutate(document["graph"]["water_result"]["resolved_coverage"]);
    auto decoded = field;
    expectFailure([&] { terrain_payload::decodeDerived(document, decoded); },
                  fragment);
  };
  reject([](auto &coverage) { coverage = nlohmann::json::array(); },
         "water coverage must be an object");
  reject([](auto &coverage) { coverage["body"] = nullptr; },
         "body array must match");
  reject([](auto &coverage) { coverage["body"].erase(std::size_t{0}); },
         "body array must match");
  reject([](auto &coverage) { coverage["body"].push_back(-1); },
         "body array must match");
  for (const auto &invalid :
       {nlohmann::json(-2), nlohmann::json(0.0), nlohmann::json(true),
        nlohmann::json(nullptr), nlohmann::json("0")}) {
    reject([&](auto &coverage) { coverage["body"][0] = invalid; },
           "water coverage body index");
  }
  reject([](auto &coverage) { coverage["body"][0] = 3; },
         "body index lies outside authoring");
  reject(
      [](auto &coverage) {
        coverage["body"][0] = std::numeric_limits<std::uint64_t>::max();
      },
      "water coverage body index");
  reject([](auto &coverage) { coverage["body"][0] = 1; },
         "references a rejected body");

  auto missing = derived;
  missing["graph"]["water_result"].erase("resolved_coverage");
  auto decoded = field;
  expectFailure([&] { terrain_payload::decodeDerived(missing, decoded); },
                "resolved_coverage");
  auto invalidGrid = field;
  invalidGrid.cellsX = 0;
  expectFailure([&] { terrain_payload::decodeDerived(derived, invalidGrid); },
                "finite positive terrain grid");
  invalidGrid = field;
  invalidGrid.size.y = std::numeric_limits<float>::infinity();
  expectFailure([&] { terrain_payload::decodeDerived(derived, invalidGrid); },
                "finite positive terrain grid");
  invalidGrid = field;
  ++invalidGrid.cellsZ;
  expectFailure([&] { terrain_payload::decodeDerived(derived, invalidGrid); },
                "terrain samples must match the grid");
}

void malformedNativeWaterCoverageEncode() {
  const auto original = fieldWithWaterCoverage();
  const auto reject = [&](const auto &mutate, std::string_view fragment) {
    auto field = original;
    auto artifacts =
        std::make_shared<TerrainGraphArtifacts>(*field.graphArtifacts);
    auto coverage = std::make_shared<terrain_water_detail::WaterLevelField>(
        *artifacts->waterResult->resolvedCoverage);
    mutate(*coverage);
    artifacts->waterResult->resolvedCoverage = std::move(coverage);
    field.graphArtifacts = std::move(artifacts);
    expectFailure(
        [&] { (void)serializeTerrainAssetPayload(field, "recipe", "inputs"); },
        fragment);
  };
  reject([](auto &coverage) { ++coverage.cellsX; }, "dimensions must match");
  reject([](auto &coverage) { ++coverage.cellsZ; }, "dimensions must match");
  reject([](auto &coverage) { coverage.size.x += 1; }, "dimensions must match");
  reject(
      [](auto &coverage) {
        coverage.size.y = std::numeric_limits<float>::quiet_NaN();
      },
      "dimensions must match");
  reject([](auto &coverage) { coverage.level.pop_back(); },
         "arrays must match");
  reject([](auto &coverage) { coverage.wet.pop_back(); }, "arrays must match");
  reject([](auto &coverage) { coverage.body.push_back(0); },
         "arrays must match");
  reject(
      [](auto &coverage) {
        coverage.level[0] = std::numeric_limits<float>::infinity();
      },
      "levels must be finite");
  reject(
      [](auto &coverage) {
        coverage.level[2] = std::numeric_limits<float>::quiet_NaN();
      },
      "levels must be finite");
  reject([](auto &coverage) { coverage.wet[0] = 2; },
         "wet flags must be zero or one");
  reject([](auto &coverage) { coverage.wet[0] = 0; },
         "wet flags must match body ownership");
  reject([](auto &coverage) { coverage.wet[2] = 1; },
         "wet flags must match body ownership");
  reject([](auto &coverage) { coverage.body[0] = 3; },
         "body index lies outside authoring");
  reject([](auto &coverage) { coverage.body[0] = 1; },
         "references a rejected body");
  reject([](auto &coverage) { coverage.level[0] = 3; },
         "level must match its authored body");
}

void graphColdLoadKeepsLocalBrush(const std::filesystem::path &root) {
  auto recipe = smallRecipe();
  recipe.graph = defaultTerrainGraph();
  auto registry = registryFor(root / "graph", recipe);
  const auto id = std::string("asset://terrain/hill");
  const auto inputs = resolveTerrainAssetGenerationInputs(registry, recipe);
  auto generated = TerrainGenerator::generate(recipe, inputs.palette.get(), {},
                                              {}, inputs.fingerprint);
  assert(generated && generated->graphArtifacts);
  const auto prepared = storeTerrainAssetPreview(
      registry, id, recipe.toJson(),
      std::make_shared<const HeightField>(std::move(*generated)));
  const auto loaded = loadTerrainAsset(registry, id);
  assert(loaded->graphArtifacts && loaded->graphArtifacts->baseField);
  auto moved = recipe;
  moved.graph["nodes"][0]["position"] = {144, 48};
  writeText(
      registry.assets.front().sourcePath,
      terrainAssetSourceJson({.id = id, .recipe = moved.toJson()}).dump(2));
  assert(prepareTerrainAsset(registry, id) == prepared);
  assert(loadTerrainAsset(registry, id) == loaded);
  auto sculpted = recipe;
  sculpted.edits.push_back({.kind = TerrainEditKind::Raise,
                            .center = {4, 3},
                            .radius = 2,
                            .amount = 1});
  const auto updated =
      updateTerrainWithInputs(recipe, sculpted, loaded, inputs);
  assert(updated);
  assert(!updated->invalidation.fullGeneration);
  assert(updated->stats.baseEvaluations == 0);
}

void appearanceDoesNotInvalidateGeneration() {
  auto recipe = smallRecipe();
  recipe.biomes.at("default").material = "asset://materials/rock";
  AssetRegistry registry;
  registry.projectDirectory = std::filesystem::temp_directory_path();
  AssetManifest material;
  material.id = "asset://materials/rock";
  material.type = "Material";
  material.sourcePath = "/this/source/is/intentionally/not/read.png";
  material.sourceHash = "first-verified-import-hash";
  material.settingsJson = "{}";
  registry.assets.push_back(std::move(material));
  const auto first = resolveTerrainAssetGenerationInputs(registry, recipe);
  assert(!first.fingerprint.empty());
  registry.assets.front().sourceHash = "second-verified-import-hash";
  const auto second = resolveTerrainAssetGenerationInputs(registry, recipe);
  assert(first.fingerprint == second.fingerprint);
  const auto digest = terrainCookRecipeDigest(recipe);
  recipe.biomes.at("default").textureScale = .25F;
  assert(terrainCookRecipeDigest(recipe) != digest);
  assert(resolveTerrainAssetGenerationInputs(registry, recipe).fingerprint ==
         first.fingerprint);
  assert(validateTerrainSurfaceReferences(registry, recipe).empty());
  registry.assets.front().type = "Texture2D";
  assert(hasErrors(validateTerrainSurfaceReferences(registry, recipe)));
  registry.assets.front().type = "DataAsset";
  registry.assets.front().settingsJson = R"({"content_type":"terrain_material"})";
  const auto unsupported = validateTerrainSurfaceReferences(registry, recipe);
  assert(!hasErrors(unsupported) && unsupported.size() == 1 &&
         unsupported.front().severity == Severity::Warning);
  registry.assets.clear();
  assert(hasErrors(validateTerrainSurfaceReferences(registry, recipe)));
}

} // namespace

int main() {
  const auto root = temporaryRoot();
  const auto cache = root / "cache";
  assert(setenv("XDG_CACHE_HOME", cache.c_str(), 1) == 0);
  try {
    completePayloadRoundTrip();
    waterCoverageRoundTrip();
    malformedWaterCoverageDecode();
    malformedNativeWaterCoverageEncode();
    sourceAndPersistentCache(root);
    graphColdLoadKeepsLocalBrush(root);
    appearanceDoesNotInvalidateGeneration();
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
  std::cout
      << "Terrain asset source, prepared cache and payload tests passed\n";
}
