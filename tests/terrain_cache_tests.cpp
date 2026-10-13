#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/assets/AssetRegistry.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Operation> void expectInvalid(Operation operation) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Malformed terrain publication was accepted");
}

TerrainRecipe smallRecipe() {
  TerrainRecipe recipe;
  recipe.size = {10, 7};
  recipe.cellsX = 7;
  recipe.cellsZ = 5;
  recipe.chunkCells = 3;
  recipe.seed = 731;
  recipe.biomes.emplace("rock", TerrainBiome{.color = {.6F, .5F, .4F, 1}});
  return recipe;
}

std::shared_ptr<const HeightField> generateField(const TerrainRecipe &recipe) {
  auto generated = TerrainGenerator::generate(recipe);
  check(generated.has_value(), "Terrain generation unexpectedly cancelled");
  return std::make_shared<const HeightField>(std::move(*generated));
}

void publicationValidation() {
  const auto recipe = smallRecipe();
  const auto json = recipe.toJson();
  const auto original = generateField(recipe);
  publishTerrain(json, "", original);

  const auto rejectMutation = [&](auto mutate) {
    HeightField malformed = *original;
    mutate(malformed);
    expectInvalid([&] {
      publishTerrain(json, "", std::make_shared<const HeightField>(std::move(malformed)));
    });
    check(findTerrain(json) == original,
          "Rejected publication replaced the prior terrain");
  };

  expectInvalid([&] { publishTerrain(json, "", nullptr); });
  check(findTerrain(json) == original,
        "Null publication replaced the prior terrain");
  rejectMutation([](HeightField &field) { field.cellsX = 0; });
  rejectMutation([](HeightField &field) { field.cellsZ += 1; });
  rejectMutation([](HeightField &field) { field.size.x += 1; });
  rejectMutation([](HeightField &field) {
    field.size.y = std::numeric_limits<float>::infinity();
  });

  for (const auto member : {&HeightField::baseHeights, &HeightField::heights}) {
    rejectMutation([&](HeightField &field) { (field.*member).pop_back(); });
    rejectMutation([&](HeightField &field) { (field.*member).push_back(0); });
    rejectMutation([&](HeightField &field) {
      (field.*member).set(0, std::numeric_limits<float>::quiet_NaN());
    });
    rejectMutation([&](HeightField &field) {
      (field.*member).set(0, std::numeric_limits<float>::infinity());
    });
  }
  rejectMutation([](HeightField &field) { field.normals.pop_back(); });
  rejectMutation(
      [](HeightField &field) { field.normals.push_back({0, 1, 0}); });
  rejectMutation([](HeightField &field) {
    auto normal = field.normals[0];
    normal.x = std::numeric_limits<float>::quiet_NaN();
    field.normals.set(0, normal);
  });
  rejectMutation([](HeightField &field) {
    auto normal = field.normals[0];
    normal.y = std::numeric_limits<float>::infinity();
    field.normals.set(0, normal);
  });
  rejectMutation([](HeightField &field) {
    auto normal = field.normals[0];
    normal.z = -std::numeric_limits<float>::infinity();
    field.normals.set(0, normal);
  });
  rejectMutation([](HeightField &field) { field.normals.set(0, {0, 0, 0}); });
  rejectMutation([](HeightField &field) { field.normals.set(0, {0, -1, 0}); });
  rejectMutation([](HeightField &field) { field.normals.set(0, {0, 2, 0}); });
  rejectMutation([](HeightField &field) { field.biomeIndices.pop_back(); });
  rejectMutation([](HeightField &field) { field.biomeIndices.push_back(0); });
  rejectMutation([](HeightField &field) {
    field.biomeIndices.set(0, field.biomeIds.size());
  });
  rejectMutation([](HeightField &field) {
    field.biomeIndices.set(0, std::numeric_limits<std::size_t>::max());
  });
  rejectMutation([](HeightField &field) { field.biomeIds.pop_back(); });
  rejectMutation([](HeightField &field) { field.biomeIds.push_back("extra"); });
  rejectMutation(
      [](HeightField &field) { field.biomeIds[1] = field.biomeIds[0]; });
  rejectMutation([](HeightField &field) {
    std::swap(field.biomeIds[0], field.biomeIds[1]);
  });
  rejectMutation([](HeightField &field) { field.biomeColors.pop_back(); });
  rejectMutation([](HeightField &field) { field.biomeColors.push_back({}); });
  rejectMutation([](HeightField &field) {
    field.biomeColors[0].a = std::numeric_limits<float>::quiet_NaN();
  });
  rejectMutation([](HeightField &field) { field.biomeColors[0].r = -1; });
  rejectMutation([](HeightField &field) { field.biomeColors[0].g = .123F; });
  rejectMutation([](HeightField &field) {
    std::swap(field.biomeColors[0], field.biomeColors[1]);
  });

  rejectMutation([](HeightField &field) { field.chunks.pop_back(); });
  rejectMutation(
      [](HeightField &field) { field.chunks.push_back(field.chunks.back()); });
  rejectMutation(
      [](HeightField &field) { std::swap(field.chunks[0], field.chunks[1]); });
  rejectMutation([](HeightField &field) { field.chunks[1] = field.chunks[0]; });
  rejectMutation([](HeightField &field) { field.chunks[0].firstCellX = -1; });
  rejectMutation([](HeightField &field) { field.chunks[0].firstCellZ = -1; });
  rejectMutation([](HeightField &field) { field.chunks[0].cellsX = 0; });
  rejectMutation([](HeightField &field) { field.chunks[0].cellsZ = -1; });
  rejectMutation([](HeightField &field) {
    field.chunks[0].firstCellX = std::numeric_limits<int>::max();
  });
  rejectMutation([](HeightField &field) {
    field.chunks[0].cellsX = std::numeric_limits<int>::max();
  });
  rejectMutation([](HeightField &field) {
    field.chunks[0].firstCellZ = std::numeric_limits<int>::max();
  });
  rejectMutation([](HeightField &field) {
    field.chunks[0].cellsZ = std::numeric_limits<int>::max();
  });
  rejectMutation([](HeightField &field) { field.chunks.back().cellsX += 1; });
  rejectMutation([](HeightField &field) { field.chunks.back().cellsZ += 1; });

  auto invalidRecipe = json;
  invalidRecipe["resolution"] = {std::numeric_limits<int>::max(),
                                 std::numeric_limits<int>::max()};
  expectInvalid([&] { publishTerrain(invalidRecipe, "", original); });
  check(findTerrain(json) == original,
        "Invalid recipe affected the prior cache entry");

  const auto replacement = generateField(recipe);
  publishTerrain(json, "", replacement);
  check(findTerrain(json) == replacement,
        "Valid publication did not replace the entry");
  check(acquireTerrain(json) == replacement,
        "Acquire ignored a published result");
}

void canonicalKeysAndWeakOwnership() {
  TerrainRecipe recipe;
  recipe.seed = 732;
  recipe.chunkCells = std::numeric_limits<int>::max();
  const auto full = recipe.toJson();
  const nlohmann::json sparse{{"format_version", 1},
                              {"seed", recipe.seed},
                              {"chunk_cells", recipe.chunkCells}};
  std::weak_ptr<const HeightField> weak;
  {
    auto field = generateField(recipe);
    publishTerrain(sparse, "", field);
    check(findTerrain(full) == field,
          "Equivalent recipe spellings produced different keys");
    check(acquireTerrain(full) == field,
          "Acquire did not use the canonical key");
    weak = field;
  }
  check(weak.expired(), "Cache retained strong ownership of a terrain");
  check(!findTerrain(full), "Expired cache entry returned a result");
  auto regenerated = acquireTerrain(sparse);
  check(regenerated && findTerrain(full) == regenerated,
        "Expired entry was not regenerated");
}

void concurrentAcquisition() {
  constexpr std::size_t workerCount = 8;
  TerrainRecipe recipe;
  recipe.cellsX = 128;
  recipe.cellsZ = 96;
  for (int round = 0; round < 3; ++round) {
    recipe.seed = 800 + round;
    const auto json = recipe.toJson();
    check(!findTerrain(json), "Concurrent test recipe was already cached");
    std::array<std::shared_ptr<const HeightField>, workerCount> results;
    std::array<std::exception_ptr, workerCount> failures;
    std::barrier start(static_cast<std::ptrdiff_t>(workerCount + 1));
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);
    for (std::size_t worker = 0; worker < workerCount; ++worker) {
      workers.emplace_back([&, worker] {
        start.arrive_and_wait();
        try {
          results[worker] = acquireTerrain(json);
        } catch (...) {
          failures[worker] = std::current_exception();
        }
      });
    }
    start.arrive_and_wait();
    for (auto &worker : workers)
      worker.join();
    for (const auto &failure : failures) {
      if (failure)
        std::rethrow_exception(failure);
    }
    check(bool(results[0]), "Concurrent acquire returned no field");
    for (const auto &result : results)
      check(result == results[0],
            "Concurrent acquire returned a noncanonical field");
    check(findTerrain(json) == results[0],
          "Cache does not retain the canonical live field");
  }
}

void representableSampleSpacing() {
  TerrainRecipe recipe;
  recipe.size = {1, 1};
  recipe.cellsX = std::numeric_limits<int>::max();
  recipe.cellsZ = 1;
  // Validation only: these cases must never allocate a dense heightfield.
  expectInvalid([&] { recipe.validate(); });
  recipe.cellsX = 1;
  recipe.cellsZ = std::numeric_limits<int>::max();
  expectInvalid([&] { recipe.validate(); });

  recipe.cellsZ = 1;
  recipe.cellsX = 1 << 24;
  recipe.validate();
  ++recipe.cellsX;
  expectInvalid([&] { recipe.validate(); });

  recipe.size.x = 1.5F;
  recipe.cellsX = 3 * (1 << 22);
  recipe.validate();
  ++recipe.cellsX;
  expectInvalid([&] { recipe.validate(); });
}

void graphLayoutIsNotAContentInput() {
  auto recipe = smallRecipe().toJson();
  recipe["graph"] = defaultTerrainGraph();
  auto moved = recipe;
  moved["graph"]["nodes"][0]["position"] = {999, -200};
  check(terrainGenerationCacheKey(recipe) == terrainGenerationCacheKey(moved),
        "Moving graph nodes changed the generation cache key");
  moved["graph"]["nodes"][0]["type"] = "constant";
  moved["graph"]["nodes"][0]["parameters"] = {{"height", 5}};
  check(terrainGenerationCacheKey(recipe) != terrainGenerationCacheKey(moved),
        "Changing a graph module did not move the generation cache key");
}

void paletteInputsAreResolvedBeforeGeneration() {
  auto project = std::filesystem::path(__FILE__).parent_path().parent_path() /
                 "examples/terrain_3d";
  if (!std::filesystem::exists(project / "demi.project.json")) {
    auto directory = std::filesystem::current_path();
    while (directory != directory.root_path()) {
      const auto candidate = directory / "examples/terrain_3d";
      if (std::filesystem::exists(candidate / "demi.project.json")) {
        project = candidate;
        break;
      }
      directory = directory.parent_path();
    }
  }
  check(std::filesystem::exists(project / "demi.project.json"),
        "Terrain palette fixture project was not found");
  const auto registry = demi::loadAssetRegistry(project);
  auto recipe = smallRecipe();
  recipe.size = {32, 32};
  recipe.cellsX = recipe.cellsZ = 32;
  recipe.paletteId = "asset://terrain/palettes/meadow";
  const auto inputs = resolveTerrainGenerationInputs(recipe, registry);
  check(inputs.palette && inputs.palette->id == recipe.paletteId &&
            !inputs.fingerprint.empty(),
        "Palette resolution did not produce a self-contained input snapshot");
  expectInvalid([&] { (void)acquireTerrain(recipe.toJson()); });
  const auto field = acquireTerrain(recipe.toJson(), inputs);
  check(field && field->resolvedPalette &&
            field->paletteId == recipe.paletteId &&
            field->inputFingerprint == inputs.fingerprint &&
            !field->scatterPlacements.empty(),
        "Cache generation lost the resolved palette");
  expectInvalid([&] { publishTerrain(recipe.toJson(), "", field); });
  check(acquireTerrain(recipe.toJson(), inputs) == field,
        "Identical palette inputs missed the generation cache");
  auto changedRegistry = registry;
  const auto surfaceSet = std::ranges::find(
      changedRegistry.assets, "asset://terrain/material_sets/meadow",
      &demi::AssetManifest::id);
  check(surfaceSet != changedRegistry.assets.end(), "Surface set is missing");
  surfaceSet->sourceHash = "changed-surface-content";
  const auto appearance =
      resolveTerrainGenerationInputs(recipe, changedRegistry);
  check(appearance.fingerprint == inputs.fingerprint &&
            acquireTerrain(recipe.toJson(), appearance) == field,
        "Surface content unnecessarily invalidated terrain generation");

  const auto manifest = std::ranges::find(
      changedRegistry.assets, recipe.paletteId, &demi::AssetManifest::id);
  std::ifstream source(manifest->sourcePath);
  auto edited = nlohmann::json::parse(source);
  edited["placements"]["grass"]["spacing"] = 7;
  const auto temporary = std::filesystem::temp_directory_path() /
                         "demi-terrain-cache-palette.json";
  {
    std::ofstream output(temporary);
    output << edited.dump();
  }
  manifest->sourcePath = temporary;
  const auto changed = resolveTerrainGenerationInputs(recipe, changedRegistry);
  std::filesystem::remove(temporary);
  check(changed.fingerprint != inputs.fingerprint,
        "Placement edits did not invalidate the palette fingerprint");
  check(!findTerrain(recipe.toJson(), changed.fingerprint),
        "Cache served a field from stale placement rules");
}
} // namespace

int main() {
  publicationValidation();
  canonicalKeysAndWeakOwnership();
  concurrentAcquisition();
  representableSampleSpacing();
  graphLayoutIsNotAContentInput();
  paletteInputsAreResolvedBeforeGeneration();
}
