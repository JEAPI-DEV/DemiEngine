#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime {

// Resolved on the owning thread before a generation job starts. The palette is
// copied into this snapshot so workers never borrow an asset registry or read
// mutable files while generating.
struct TerrainGenerationInputs {
  std::shared_ptr<const TerrainPalette> palette;
  std::string fingerprint;
};

[[nodiscard]] TerrainGenerationInputs
resolveTerrainGenerationInputs(const TerrainRecipe &recipe,
                               const AssetRegistry &registry);

// The cache key for a recipe: the generator version, the canonical recipe, and
// a fingerprint of the resolved input assets. Exposed so tooling and tests can
// confirm that changing any of the three actually moves the key.
std::string terrainGenerationCacheKey(const nlohmann::json &recipe,
                                      std::string_view inputFingerprint = {});

// Immutable generation results are shared between editor publication and scene
// loading. The cache holds weak references: live worlds/controllers own memory.
// Keys are canonical recipes, not file paths or mutable scene identities.
// The fingerprint covers the resolved palette's generation-relevant fields and
// referenced asset source hashes. An id alone cannot detect changed roles or
// asset content.
std::shared_ptr<const HeightField>
findTerrain(const nlohmann::json &recipe, std::string_view inputFingerprint = {});
// Validates dimensions, dense arrays, finite heights/upward unit normals,
// biome metadata, and exact recipe chunk layout before replacing an entry.
// Invalid publications throw invalid_argument and preserve the previous entry.
void publishTerrain(const nlohmann::json &recipe,
                    std::string_view inputFingerprint,
                    std::shared_ptr<const HeightField> field);
// Generation/validation occur outside the mutex. Concurrent misses may compute
// in parallel, but a second lookup returns the same live canonical result.
std::shared_ptr<const HeightField>
acquireTerrain(const nlohmann::json &recipe, std::string_view inputFingerprint = {});
std::shared_ptr<const HeightField>
acquireTerrain(const nlohmann::json &recipe,
               const TerrainGenerationInputs &inputs);
} // namespace demi::runtime
