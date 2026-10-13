#pragma once

#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"
#include "demi/runtime/terrain/TerrainLod.h"

#include <compare>

namespace demi::runtime::render {

// Field-independent identity: identical dimensions/detail share one GPU mesh
// across chunks, terrain assets and transformed instances.
struct TerrainPatchTopologyKey3D {
  int cellsX = 0;
  int cellsZ = 0;
  TerrainLod level = TerrainLod::Full;

  auto operator<=>(const TerrainPatchTopologyKey3D &) const = default;
};

// Indexed local sample-grid geometry, with zero height and normalized patch
// UVs. Displacement consumes positions.x/z as sample offsets. Every perimeter
// sample survives reduction, so mixed levels share complete boundary segments.
// Throws invalid_argument for invalid dimensions/detail and length_error when
// vertex/index counts cannot be addressed by the 32-bit GPU mesh interface.
[[nodiscard]] PrimitiveMeshData3D
createTerrainPatchTopology3D(const TerrainPatchTopologyKey3D &key);

} // namespace demi::runtime::render
