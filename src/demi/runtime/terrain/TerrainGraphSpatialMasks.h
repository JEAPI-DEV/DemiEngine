#pragma once

#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

namespace demi::runtime::terrain_graph_detail {

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
