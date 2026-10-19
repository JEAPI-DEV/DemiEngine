#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <nlohmann/json_fwd.hpp>

namespace demi::runtime {
struct TerrainWaterBodySpec;

void validateTerrainWaterFlow(const TerrainWaterBodySpec &body);
[[nodiscard]] bool sameTerrainWaterFlow(const TerrainWaterBodySpec &before,
                                        const TerrainWaterBodySpec &after);
void parseTerrainWaterFlow(const nlohmann::json &value,
                           TerrainWaterBodySpec &body);

// Terrain-local units per second. Rivers follow their nearest nondegenerate
// authored XZ path segment; ties retain source segment order. Lakes and oceans
// use the authored direction. This prescribed current is not drainage physics.
[[nodiscard]] Vec3 terrainWaterFlowVelocity(const TerrainWaterBodySpec &body,
                                            Vec3 localPosition,
                                            Vec3 surfaceNormal = {0, 1, 0});
} // namespace demi::runtime
