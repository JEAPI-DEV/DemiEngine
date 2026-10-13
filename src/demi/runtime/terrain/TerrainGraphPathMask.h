#pragma once

#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

namespace demi::runtime::terrain_graph_detail {

// Measures terrain-local horizontal distance; source elevation never changes
// the authored corridor width. The source is retained for grid compatibility.
[[nodiscard]] std::optional<GraphNodeResult>
evaluateTerrainPathMask(const nlohmann::json &parameters, GraphField source,
                        std::stop_token stop);

} // namespace demi::runtime::terrain_graph_detail
