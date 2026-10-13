#pragma once

#include <stop_token>

namespace demi::runtime {
struct HeightField;
struct TerrainRecipe;
struct TerrainDrainage;
struct TerrainMasks;
namespace terrain_water_detail {
struct WaterLevelField;
}
template <typename T> class TerrainSamples;

// Derive all material/scatter masks from the final surface and drainage.
bool buildTerrainMasks(const HeightField &field, const TerrainRecipe &recipe,
                       const TerrainDrainage *drainage,
                       const TerrainSamples<float> *sediment, TerrainMasks &out,
                       bool includeFlow, std::stop_token stop = {});
// Overrides inferred sea masks using the exact accepted water-body coverage.
bool applyTerrainWaterMasks(
    const HeightField &field,
    const terrain_water_detail::WaterLevelField &coverage, TerrainMasks &masks,
    std::stop_token stop = {});
} // namespace demi::runtime
