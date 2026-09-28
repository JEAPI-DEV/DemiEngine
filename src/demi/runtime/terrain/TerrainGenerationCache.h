#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace demi::runtime {

// Immutable generation results are shared between editor publication and scene
// loading. The cache holds weak references: live worlds/controllers own memory.
// Keys are canonical recipes, not file paths or mutable scene identities.
std::shared_ptr<const HeightField> findTerrain(const nlohmann::json &recipe);
// Validates dimensions, dense arrays, finite heights/upward unit normals,
// biome metadata, and exact recipe chunk layout before replacing an entry.
// Invalid publications throw invalid_argument and preserve the previous entry.
void publishTerrain(const nlohmann::json &recipe,
                    std::shared_ptr<const HeightField> field);
// Generation/validation occur outside the mutex. Concurrent misses may compute
// in parallel, but a second lookup returns the same live canonical result.
std::shared_ptr<const HeightField> acquireTerrain(const nlohmann::json &recipe);
} // namespace demi::runtime
