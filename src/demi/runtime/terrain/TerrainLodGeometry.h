#pragma once

#include "demi/runtime/terrain/TerrainLod.h"

namespace demi::runtime::terrain_detail {

struct TerrainLodSample {
  int cellX = 0;
  int cellZ = 0;
};

// Shared topology for mesh construction and exact geometric-error preparation.
struct TerrainLodTopology {
  std::vector<TerrainLodSample> samples;
  std::vector<std::size_t> indices;
  std::vector<TerrainLodTriangle> triangles;
  std::vector<std::size_t> boundary;
  std::size_t columns = 0;
  std::size_t rows = 0;
};

[[nodiscard]] TerrainLodTopology buildTerrainLodTopology(const TerrainChunk &,
                                                         TerrainLod);

} // namespace demi::runtime::terrain_detail
