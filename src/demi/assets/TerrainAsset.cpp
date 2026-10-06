#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainSurfaceReferences.h"

#include "demi/assets/TerrainAssetPayload.h"
#include "demi/assets/TerrainAssetStorage.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"

#include <stdexcept>

namespace demi::assets {
namespace {

const AssetManifest &terrainManifest(const AssetRegistry &registry,
                                     std::string_view id) {
  const auto *manifest = findAsset(registry, std::string(id));
  if (!manifest)
    throw std::invalid_argument("Terrain asset was not found: " +
                                std::string(id));
  if (manifest->type != "Terrain" ||
      manifest->importer != "terrain_heightfield")
    throw std::invalid_argument("Terrain asset must have type Terrain and "
                                "importer terrain_heightfield: " +
                                std::string(id));
  const auto filename = manifest->sourcePath.filename().string();
  if (!filename.ends_with(".terrain.json") &&
      !filename.ends_with(".terrain.bin"))
    throw std::invalid_argument("Terrain asset source has an unknown suffix: " +
                                manifest->sourcePath.string());
  return *manifest;
}

struct SourceInputs {
  TerrainAssetSource source;
  runtime::TerrainRecipe parsed;
  runtime::TerrainGenerationInputs generation;
  std::string recipeDigest;
  std::string inputFingerprint;
  std::filesystem::path cachePath;
};

SourceInputs resolveSource(const AssetRegistry &registry,
                           const AssetManifest &manifest) {
  SourceInputs inputs;
  inputs.source = loadTerrainAssetSource(manifest);
  inputs.parsed = runtime::TerrainRecipe::parse(inputs.source.recipe);
  inputs.generation =
      resolveTerrainAssetGenerationInputs(registry, inputs.parsed);
  inputs.recipeDigest = runtime::terrainCookRecipeDigest(inputs.parsed);
  inputs.inputFingerprint = inputs.generation.fingerprint;
  inputs.cachePath = terrain_storage::cachePath(
      registry, manifest.id, inputs.recipeDigest, inputs.inputFingerprint);
  return inputs;
}

void verifyPrepared(const SourceInputs &source,
                    const terrain_storage::PreparedField &prepared) {
  if (prepared.recipeDigest != source.recipeDigest ||
      prepared.inputFingerprint != source.inputFingerprint)
    throw std::runtime_error(
        "Prepared terrain is stale after its recipe or asset inputs changed");
  runtime::publishTerrain(source.source.recipe, source.inputFingerprint,
                          prepared.field);
}

} // namespace

std::vector<std::byte>
serializeTerrainAssetPayload(const runtime::HeightField &field,
                             std::string_view recipeDigest,
                             std::string_view inputFingerprint) {
  return terrain_payload::encode(field, recipeDigest, inputFingerprint);
}

std::shared_ptr<const runtime::HeightField>
deserializeTerrainAssetPayload(std::span<const std::byte> payload) {
  return terrain_payload::decode(payload);
}

runtime::TerrainGenerationInputs
resolveTerrainAssetGenerationInputs(const AssetRegistry &registry,
                                    const runtime::TerrainRecipe &recipe) {
  const auto diagnostics = validateTerrainSurfaceReferences(registry, recipe);
  for (const auto &diagnostic : diagnostics)
    if (diagnostic.severity == Severity::Error)
      throw std::invalid_argument(diagnostic.message);
  auto inputs = runtime::resolveTerrainGenerationInputs(recipe, registry);
  inputs.fingerprint =
      terrain_storage::assetInputFingerprint(registry, recipe, inputs);
  return inputs;
}

std::shared_ptr<const runtime::HeightField>
loadTerrainAsset(const AssetRegistry &registry, std::string_view assetId,
                 nlohmann::json *sourceRecipe) {
  if (sourceRecipe)
    *sourceRecipe = nullptr;
  const auto &manifest = terrainManifest(registry, assetId);
  if (isPreparedTerrainAsset(manifest))
    return terrain_storage::read(manifest.sourcePath).field;

  const auto source = resolveSource(registry, manifest);
  terrain_storage::PreparedField prepared;
  try {
    prepared = terrain_storage::read(source.cachePath);
    verifyPrepared(source, prepared);
  } catch (const std::exception &error) {
    throw std::runtime_error(
        "Terrain asset " + manifest.id +
        " has no usable prepared cache for its current recipe and inputs; it "
        "may be missing or stale (" +
        error.what() +
        "). Run terrain preparation in the editor or build before loading.");
  }
  if (sourceRecipe)
    *sourceRecipe = source.source.recipe;
  return prepared.field;
}

std::filesystem::path prepareTerrainAsset(const AssetRegistry &registry,
                                          std::string_view assetId,
                                          std::stop_token stop) {
  const auto &manifest = terrainManifest(registry, assetId);
  if (isPreparedTerrainAsset(manifest))
    return manifest.sourcePath;
  const auto source = resolveSource(registry, manifest);
  if (std::filesystem::exists(source.cachePath)) {
    try {
      const auto prepared = terrain_storage::read(source.cachePath);
      verifyPrepared(source, prepared);
      return source.cachePath;
    } catch (const std::exception &) {
      // Explicit preparation can replace a stale or damaged cache. The prior
      // file stays intact until a complete replacement has been encoded.
    }
  }
  if (stop.stop_requested())
    throw std::runtime_error("Terrain preparation cancelled before generation");
  auto generated = runtime::TerrainGenerator::generate(
      source.parsed, source.generation.palette.get(), stop, {},
      source.inputFingerprint);
  if (!generated || stop.stop_requested())
    throw std::runtime_error("Terrain preparation cancelled during generation");
  return storeTerrainAssetPreview(
      registry, assetId, source.source.recipe,
      std::make_shared<const runtime::HeightField>(std::move(*generated)));
}

std::filesystem::path
storeTerrainAssetPreview(const AssetRegistry &registry,
                         std::string_view assetId, const nlohmann::json &recipe,
                         std::shared_ptr<const runtime::HeightField> field) {
  if (!field)
    throw std::invalid_argument("Cannot store an empty terrain preview");
  const auto &manifest = terrainManifest(registry, assetId);
  if (isPreparedTerrainAsset(manifest))
    throw std::invalid_argument("A shipped terrain payload cannot be edited");
  const auto proposed = runtime::TerrainRecipe::parse(recipe);
  const auto generation =
      resolveTerrainAssetGenerationInputs(registry, proposed);
  const auto recipeDigest = runtime::terrainCookRecipeDigest(proposed);
  const auto &inputFingerprint = generation.fingerprint;
  const auto target = terrain_storage::cachePath(
      registry, assetId, recipeDigest, inputFingerprint);
  auto prepared = std::make_shared<runtime::HeightField>(*field);
  prepared->inputFingerprint = inputFingerprint;
  if (generation.palette) {
    if (!prepared->resolvedPalette || prepared->paletteId != proposed.paletteId)
      throw std::invalid_argument(
          "Terrain preview does not contain its resolved palette");
  }
  runtime::publishTerrain(recipe, inputFingerprint, prepared);
  const auto bytes =
      serializeTerrainAssetPayload(*prepared, recipeDigest, inputFingerprint);
  terrain_storage::writeAtomically(target, bytes);
  return target;
}

void prepareTerrainAssets(const AssetRegistry &registry) {
  for (const auto &manifest : registry.assets)
    if (manifest.type == "Terrain" &&
        manifest.importer == "terrain_heightfield" && !isPreparedTerrainAsset(manifest))
      (void)prepareTerrainAsset(registry, manifest.id);
}

} // namespace demi::assets
