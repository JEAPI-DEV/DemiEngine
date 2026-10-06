#pragma once

#include "demi/runtime/terrain/TerrainWater.h"

namespace demi::runtime {
// Warnings only: explicitly authored levels are not silently clamped or banks
// invented. This is finite-body containment checking, not fluid simulation.
std::vector<std::string>
terrainWaterContainmentWarnings(const HeightField &ground,
                                const TerrainWaterAuthoring &authoring);
} // namespace demi::runtime
