#pragma once

#include <cstddef>
#include <span>
#include <stop_token>

namespace demi::runtime {
struct TerrainWaterBodySpec;
struct TerrainWaterSurface;
namespace terrain_water_detail {
struct WaterLevelField;

// Tessellation consumes prepared coverage; it never resolves connectivity or
// carves terrain. Gameplay queries do not depend on this representation.
TerrainWaterSurface buildTerrainWaterSurface(const TerrainWaterBodySpec &body,
                                             std::size_t bodyIndex,
                                             const WaterLevelField &coverage,
                                             std::span<const float> ground,
                                             std::stop_token stop = {});
} // namespace terrain_water_detail
} // namespace demi::runtime
