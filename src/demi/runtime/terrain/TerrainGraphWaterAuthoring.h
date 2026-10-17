#pragma once

#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

namespace demi::runtime::terrain_graph_detail {
[[nodiscard]] std::optional<GraphNodeResult>
evaluateTerrainWaterEdit(const nlohmann::json &parameters,
                         const GraphValues &connected, std::stop_token stop);
[[nodiscard]] std::optional<GraphNodeResult>
evaluateTerrainBasinWater(const TerrainGraphNode &node,
                          const nlohmann::json &parameters,
                          const GraphValues &connected, std::stop_token stop);
} // namespace demi::runtime::terrain_graph_detail
