#pragma once

#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

// Private native boundary between graph scheduling and the current node types.
namespace demi::runtime::terrain_graph_detail {

using GraphField = std::shared_ptr<const HeightField>;

struct GraphMask {
  GraphField source;
  TerrainSamples<float> samples;
};

struct GraphDrainage {
  GraphField source;
  TerrainDrainage data;
};

struct GraphWater {
  TerrainWaterAuthoring authoring;
  TerrainWaterResult data;
};

struct GraphInstances {
  std::vector<TerrainScatterPlacement> placements;
  bool truncated = false;
};

using GraphValue =
    std::variant<GraphField, std::shared_ptr<const GraphMask>,
                 std::shared_ptr<const GraphDrainage>,
                 std::shared_ptr<const GraphWater>,
                 std::shared_ptr<const GraphInstances>>;
using GraphValues = std::map<std::string, GraphValue>;

struct GraphNodeResult {
  GraphValues outputs;
  std::vector<std::string> warnings;
};

[[nodiscard]] nlohmann::json terrainGraphNodeParameters(
    const TerrainGraphNode &node);
[[nodiscard]] nlohmann::json terrainGraphNodeContext(
    const TerrainRecipe &recipe, const TerrainGraphNode &node,
    const TerrainGraphInputs &inputs);

// Empty only on cancellation. Invalid node inputs surface to the executor for
// node-id attribution; graph topology and cache decisions stay outside here.
[[nodiscard]] std::optional<GraphNodeResult> evaluateTerrainGraphNode(
    const TerrainGraphNode &node, const nlohmann::json &parameters,
    const TerrainRecipe &sourceRecipe, const TerrainRecipe &authoredRecipe,
    const TerrainGraphInputs &inputs, const GraphValues &connected,
    std::stop_token stop);

} // namespace demi::runtime::terrain_graph_detail
