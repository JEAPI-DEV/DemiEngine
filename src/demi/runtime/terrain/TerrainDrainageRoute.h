#pragma once

#include "demi/runtime/terrain/TerrainDrainage.h"

namespace demi::runtime {

enum class TerrainDrainageRouteEndpoint {
  SeaLevel,
  FieldEdge,
  ClosedBasin,
  BlockedSurface
};

struct TerrainDrainageRoute {
  std::vector<std::size_t> samples;
  std::vector<Vec3> points;
  TerrainDrainageRouteEndpoint endpoint =
      TerrainDrainageRouteEndpoint::BlockedSurface;
  std::size_t basin = 0;
};

// Follows drainage guidance along nonascending edges of the full-resolution
// terrain triangles. Basin guidance stops at a sink or an uphill link rather
// than inventing a spill route;
// BlockedSurface identifies guidance the actual surface cannot safely follow.
// Invalid storage/links/cycles throw; cancellation alone returns nullopt.
[[nodiscard]] std::optional<TerrainDrainageRoute>
traceTerrainDrainageRoute(const HeightField &field,
                          const TerrainDrainage &drainage,
                          std::size_t sourceSample, std::stop_token stop = {});

} // namespace demi::runtime
