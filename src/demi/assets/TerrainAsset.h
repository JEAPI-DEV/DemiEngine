#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <stop_token>
#include <vector>

namespace demi::assets {

// Bump when the complete prepared payload envelope or derived-data contract
// changes. Cook settings and the persistent cache key use this version.
inline constexpr int terrainAssetPayloadVersion = 4;

struct TerrainAssetSource {
  int formatVersion = 1;
  std::string id;
  std::string name;
  nlohmann::json recipe;
};

inline bool isPreparedTerrainAsset(const AssetManifest &manifest) {
  return manifest.type == "Terrain" &&
         manifest.sourcePath.filename().string().ends_with(".terrain.bin");
}

// Authoring source is separate from the generated runtime payload.
TerrainAssetSource parseTerrainAssetSource(const nlohmann::json &document);
TerrainAssetSource loadTerrainAssetSource(const AssetManifest &manifest);
nlohmann::json terrainAssetSourceJson(const TerrainAssetSource &source);
std::vector<std::string>
terrainAssetDependencies(const TerrainAssetSource &source);

std::vector<std::byte>
serializeTerrainAssetPayload(const runtime::HeightField &field,
                             std::string_view recipeDigest,
                             std::string_view inputFingerprint);
std::shared_ptr<const runtime::HeightField>
deserializeTerrainAssetPayload(std::span<const std::byte> payload);

// Snapshot all generation-relevant asset inputs once. The returned fingerprint
// is the same one stored in prepared payloads and supplied to incremental
// terrain updates, including recipes with no palette. The registry is a
// validated import snapshot; refresh/reimport it when source files change.
runtime::TerrainGenerationInputs
resolveTerrainAssetGenerationInputs(const AssetRegistry &registry,
                                    const runtime::TerrainRecipe &recipe);

// Loads an already prepared source cache or a shipped payload. Never generates.
// Source recipes are returned only for editable source assets, not shipped
// data.
std::shared_ptr<const runtime::HeightField>
loadTerrainAsset(const AssetRegistry &registry, std::string_view assetId,
                 nlohmann::json *sourceRecipe = nullptr);

// Authoring/build preparation. Persistent outputs use platform cache
// directories. The field overload stores an editor-generated preview without
// generating again.
std::filesystem::path prepareTerrainAsset(const AssetRegistry &registry,
                                          std::string_view assetId,
                                          std::stop_token stop = {});
std::filesystem::path
storeTerrainAssetPreview(const AssetRegistry &registry,
                         std::string_view assetId, const nlohmann::json &recipe,
                         std::shared_ptr<const runtime::HeightField> field);
void prepareTerrainAssets(const AssetRegistry &registry);

} // namespace demi::assets
