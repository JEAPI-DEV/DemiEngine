#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainGraph.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace demi::runtime {
namespace {
struct GenerationCache {
  std::mutex mutex;
  std::map<std::string, std::weak_ptr<const HeightField>> fields;
};

GenerationCache &cache() {
  // Process-wide deduplication only; no gameplay state or strong asset
  // ownership.
  static GenerationCache instance;
  return instance;
}

// The key mixes the generator version, semantic recipe and resolved input
// fingerprint. Graph layout belongs to the authored document but not to the
// generated field, so only the graph's content key enters this cache.
std::string recipeKey(const TerrainRecipe &parsed,
                      std::string_view inputFingerprint) {
  auto semantic = parsed.toJson();
  if (!parsed.graph.is_null())
    semantic["graph"] = TerrainGraph::parse(parsed.graph).contentKey();
  return std::to_string(terrainGeneratorVersion) + "\n" +
         semantic.dump() + "\n" + std::string(inputFingerprint);
}

std::string paletteFingerprint(const TerrainPalette &palette,
                               const AssetRegistry &registry) {
  nlohmann::json semantic{{"format_version", palette.formatVersion},
                          {"id", palette.id},
                          {"material_set", palette.materialSet},
                          {"placements", nlohmann::json::object()},
                          {"assets", nlohmann::json::object()}};
  for (const auto &[name, entry] : palette.placements)
    semantic["placements"][name] = {
        {"model", entry.model},
        {"prefab", entry.prefab},
        {"weight", entry.weight},
        {"scale", {entry.scaleMin, entry.scaleMax}},
        {"spacing", entry.spacing},
        {"collision", std::string(terrainCollisionPolicyName(entry.collision))},
        {"lod", entry.lod},
        {"biomes", entry.biomes}};
  for (const auto &id : palette.assetDependencies()) {
    if (!id.starts_with("asset://") || id == palette.materialSet)
      continue;
    const auto *asset = findAsset(registry, id);
    if (asset == nullptr || asset->sourceHash.empty())
      throw std::invalid_argument("Terrain palette dependency has no content "
                                  "hash: " + id);
    semantic["assets"][id] = asset->sourceHash;
  }
  const std::string serialized = semantic.dump();
  return assets::hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(serialized.data()),
      serialized.size()));
}

void validateInputs(const TerrainRecipe &recipe,
                    const TerrainGenerationInputs &inputs) {
  if (recipe.paletteId.empty()) {
    if (inputs.palette)
      throw std::invalid_argument(
          "Terrain generation inputs name a palette absent from the recipe");
    return;
  }
  if (!inputs.palette || inputs.palette->id != recipe.paletteId ||
      inputs.fingerprint.empty())
    throw std::invalid_argument("Terrain palette " + recipe.paletteId +
                                " must be resolved before generation");
}

void validateField(const TerrainRecipe &recipe, const HeightField &field) {
  if (field.cellsX != recipe.cellsX || field.cellsZ != recipe.cellsZ ||
      field.size.x != recipe.size.x || field.size.y != recipe.size.y)
    throw std::invalid_argument(
        "Terrain result dimensions do not match its recipe");

  const auto sampleCount = recipe.sampleCount();
  if (field.baseHeights.size() != sampleCount ||
      field.heights.size() != sampleCount ||
      field.normals.size() != sampleCount ||
      field.biomeIndices.size() != sampleCount || field.exclusions.size() != sampleCount)
    throw std::invalid_argument(
        "Terrain result arrays do not match its sample count");

  if (field.biomeIds.size() != recipe.biomes.size() ||
      field.biomeColors.size() != recipe.biomes.size())
    throw std::invalid_argument(
        "Terrain result biome metadata does not match its recipe");

  std::size_t biomeIndex = 0;
  for (const auto &[id, biome] : recipe.biomes) {
    const auto color = field.biomeColors[biomeIndex];
    if (field.biomeIds[biomeIndex] != id || !std::isfinite(color.r) ||
        !std::isfinite(color.g) || !std::isfinite(color.b) ||
        !std::isfinite(color.a) || color.r != biome.color.r ||
        color.g != biome.color.g || color.b != biome.color.b ||
        color.a != biome.color.a)
      throw std::invalid_argument(
          "Terrain result biome IDs/colors do not match its recipe");
    ++biomeIndex;
  }

  for (std::size_t sample = 0; sample < sampleCount; ++sample) {
    if (!std::isfinite(field.baseHeights[sample]) ||
        !std::isfinite(field.heights[sample]) ||
        !std::isfinite(field.exclusions[sample]) || field.exclusions[sample] < 0 ||
        field.exclusions[sample] > 1)
      throw std::invalid_argument("Terrain result contains a nonfinite height");
    const auto normal = field.normals[sample];
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
        !std::isfinite(normal.z))
      throw std::invalid_argument("Terrain result contains a nonfinite normal");
    const double length =
        std::hypot(double(normal.x), double(normal.y), double(normal.z));
    if (normal.y < 0 || std::abs(length - 1) > 1e-4)
      throw std::invalid_argument(
          "Terrain result normals must be upward unit vectors");
    if (field.biomeIndices[sample] >= recipe.biomes.size())
      throw std::invalid_argument(
          "Terrain result contains an invalid biome index");
  }

  const auto chunksX =
      (std::size_t(recipe.cellsX) - 1) / std::size_t(recipe.chunkCells) + 1;
  const auto chunksZ =
      (std::size_t(recipe.cellsZ) - 1) / std::size_t(recipe.chunkCells) + 1;
  // parse() validated the recipe's chunk-count multiplication. Never add or
  // multiply untrusted chunk ranges; compare them to bounded expected values.
  if (field.chunks.size() != chunksX * chunksZ)
    throw std::invalid_argument(
        "Terrain result chunk count does not match its recipe");
  std::size_t chunkIndex = 0;
  for (int z = 0; z < recipe.cellsZ;) {
    const int depth = std::min(recipe.chunkCells, recipe.cellsZ - z);
    for (int x = 0; x < recipe.cellsX;) {
      const int width = std::min(recipe.chunkCells, recipe.cellsX - x);
      const auto &chunk = field.chunks[chunkIndex];
      if (chunk.firstCellX != x || chunk.firstCellZ != z ||
          chunk.cellsX != width || chunk.cellsZ != depth)
        throw std::invalid_argument(
            "Terrain result chunk layout does not match its recipe");
      ++chunkIndex;
      x += width;
    }
    z += depth;
  }
}

// Call only while holding the cache mutex. Returning a strong pointer keeps
// the chosen result alive after the lock is released.
std::shared_ptr<const HeightField> findLocked(GenerationCache &store,
                                              const std::string &key) {
  const auto found = store.fields.find(key);
  return found == store.fields.end() ? nullptr : found->second.lock();
}

void removeExpiredLocked(GenerationCache &store) {
  std::erase_if(store.fields,
                [](const auto &entry) { return entry.second.expired(); });
}
} // namespace

TerrainGenerationInputs
resolveTerrainGenerationInputs(const TerrainRecipe &recipe,
                               const AssetRegistry &registry) {
  if (recipe.paletteId.empty())
    return {};
  auto loaded = loadTerrainPalette(registry, recipe.paletteId);
  if (!loaded)
    throw std::invalid_argument("Terrain palette did not resolve: " +
                                recipe.paletteId);
  TerrainGenerationInputs inputs;
  inputs.fingerprint = paletteFingerprint(*loaded, registry);
  inputs.palette = std::make_shared<const TerrainPalette>(std::move(*loaded));
  return inputs;
}

std::string terrainGenerationCacheKey(const nlohmann::json &recipe,
                                      std::string_view inputFingerprint) {
  return recipeKey(TerrainRecipe::parse(recipe), inputFingerprint);
}

std::shared_ptr<const HeightField>
findTerrain(const nlohmann::json &recipe, std::string_view inputFingerprint) {
  const auto key = recipeKey(TerrainRecipe::parse(recipe), inputFingerprint);
  auto &store = cache();
  std::scoped_lock lock(store.mutex);
  return findLocked(store, key);
}

void publishTerrain(const nlohmann::json &recipe,
                    std::string_view inputFingerprint,
                    std::shared_ptr<const HeightField> field) {
  if (!field)
    throw std::invalid_argument("Cannot publish an empty terrain result");
  const auto parsed = TerrainRecipe::parse(recipe);
  validateField(parsed, *field);
  if (!parsed.paletteId.empty() &&
      (inputFingerprint.empty() || !field->resolvedPalette ||
       field->paletteId != parsed.paletteId ||
       field->inputFingerprint != inputFingerprint))
    throw std::invalid_argument(
        "Published terrain does not match its resolved palette inputs");
  const auto key = recipeKey(parsed, inputFingerprint);
  auto &store = cache();
  std::scoped_lock lock(store.mutex);
  removeExpiredLocked(store);
  store.fields[key] = std::move(field);
}

std::shared_ptr<const HeightField>
acquireTerrain(const nlohmann::json &recipe, std::string_view inputFingerprint) {
  if (!TerrainRecipe::parse(recipe).paletteId.empty())
    throw std::invalid_argument(
        "Palette terrain requires resolved generation inputs");
  return acquireTerrain(recipe, TerrainGenerationInputs{
                                    .palette = {},
                                    .fingerprint = std::string(inputFingerprint)});
}

std::shared_ptr<const HeightField>
acquireTerrain(const nlohmann::json &recipe,
               const TerrainGenerationInputs &inputs) {
  const auto parsed = TerrainRecipe::parse(recipe);
  validateInputs(parsed, inputs);
  const auto key = recipeKey(parsed, inputs.fingerprint);
  auto &store = cache();
  {
    std::scoped_lock lock(store.mutex);
    if (auto existing = findLocked(store, key))
      return existing;
  }
  auto generated = TerrainGenerator::generate(
      parsed, inputs.palette.get(), {}, {}, inputs.fingerprint);
  if (!generated)
    throw std::runtime_error("Terrain generation unexpectedly cancelled");
  auto field = std::make_shared<const HeightField>(std::move(*generated));
  validateField(parsed, *field);
  if (inputs.palette &&
      (field->paletteId != parsed.paletteId ||
       field->inputFingerprint != inputs.fingerprint ||
       !field->resolvedPalette))
    throw std::logic_error("Terrain generator dropped resolved palette inputs");
  std::scoped_lock lock(store.mutex);
  if (auto existing = findLocked(store, key))
    return existing;
  removeExpiredLocked(store);
  store.fields[key] = field;
  return field;
}
} // namespace demi::runtime
