#pragma once

#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

namespace demi::runtime::terrain_graph_detail {

struct TerrainSpatialMaskPoint {
  double x = 0;
  double z = 0;
};

struct TerrainSpatialMask {
  std::vector<TerrainSpatialMaskPoint> points;
  bool polygon = false;
  double width = 0;
  double falloff = 0;
};

[[nodiscard]] std::optional<TerrainSpatialMask>
parseTerrainSpatialMask(const nlohmann::json &parameters, bool polygon,
                        std::stop_token stop = {});
[[nodiscard]] std::optional<float>
sampleTerrainSpatialMask(const TerrainSpatialMask &mask, double x, double z,
                         std::stop_token stop = {});

// Shared authored-point validation; throws an actionable invalid_argument.
void validateTerrainSpatialMaskPoints(const nlohmann::json &points,
                                      bool polygon);

// Measures terrain-local horizontal distance; source elevation never changes
// the authored corridor width. The source is retained for grid compatibility.
[[nodiscard]] std::optional<GraphNodeResult>
evaluateTerrainPathMask(const nlohmann::json &parameters, GraphField source,
                        std::stop_token stop);

// Filled terrain-local X/Z polygon, either winding. Repeated endpoints are
// allowed; self intersections use even-odd filling. Boundary samples are one,
// and only the exterior fades over the authored world-unit falloff.
[[nodiscard]] std::optional<GraphNodeResult>
evaluateTerrainPolygonMask(const nlohmann::json &parameters, GraphField source,
                           std::stop_token stop);

} // namespace demi::runtime::terrain_graph_detail
