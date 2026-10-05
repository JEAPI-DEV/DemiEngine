#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainUpdate.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  TerrainPaletteEntry role;
  role.role = TerrainPaletteRole::Tree;
  role.asset = "asset://tree";
  palette->roles.emplace("tree", role);
  field.resolvedPalette = palette;
  TerrainScatterPlacement tree;
  tree.role = TerrainPaletteRole::Tree;
  tree.asset = role.asset;
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
         restored->resolvedPalette->roles.size() == 1);
  assert(restored->graphArtifacts && restored->graphArtifacts->baseField);
  assert(restored->graphArtifacts->basePlacements->size() == 1);
  assert(restored->graphArtifacts->water.bodies.size() == 1);
  assert(restored->graphArtifacts->waterResult->surfaces.size() == 1);
  assert(restored->graphArtifacts->waterResult->carvedHeights == field.heights);
  payload.back() ^= std::byte{1};
  expectFailure([&] { (void)deserializeTerrainAssetPayload(payload); },
                "checksum");
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

void fingerprintUsesRegistryMetadataWithoutReadingArt() {
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
  assert(first.fingerprint != second.fingerprint);
}

} // namespace

int main() {
  const auto root = temporaryRoot();
  const auto cache = root / "cache";
  assert(setenv("XDG_CACHE_HOME", cache.c_str(), 1) == 0);
  try {
    completePayloadRoundTrip();
    sourceAndPersistentCache(root);
    graphColdLoadKeepsLocalBrush(root);
    fingerprintUsesRegistryMetadataWithoutReadingArt();
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
  std::cout
      << "Terrain asset source, prepared cache and payload tests passed\n";
}
