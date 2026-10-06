#pragma once

#include "demi/runtime/terrain/TerrainUpdate.h"

namespace demi::runtime {

// Conservative sample footprint, shared by full replay and incremental edits.
// A border accounts for sample rounding and dependencies such as normals.
TerrainRect terrainBrushSampleBounds(const HeightField &field, Vec2 center,
                                     float radius, int border = 1);

} // namespace demi::runtime
