#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include <optional>

namespace demi::runtime {
// Queries use terrain-local coordinates and the same cell diagonal as the mesh.
std::optional<float> sampleTerrainHeight(const HeightField &field,
                                         Vec2 position);
// Direction need not be normalized. Visits crossed grid cells rather than
// scanning every triangle; returns the nearest forward surface intersection.
std::optional<Vec3> raycastTerrain(const HeightField &field, Vec3 origin,
                                   Vec3 direction);
} // namespace demi::runtime
