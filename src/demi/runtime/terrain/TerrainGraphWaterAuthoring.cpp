#include "demi/runtime/terrain/TerrainGraphWaterAuthoring.h"

#include "demi/runtime/terrain/TerrainWaterAuthoring.h"
#include "demi/runtime/terrain/TerrainWaterProposals.h"

#include <algorithm>
#include <stdexcept>

namespace demi::runtime::terrain_graph_detail {
namespace {
std::optional<GraphNodeResult> publishWater(GraphField field,
                                            TerrainWaterAuthoring authoring,
                                            std::stop_token stop) {
  auto resolved =
      refreshTerrainWaterResult(*field, authoring, field->heights, stop);
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (!resolved || resolved->dropped != 0) {
    throw std::invalid_argument(
        "Edited/proposed water could not be resolved on its input terrain");
  }
  auto water = std::make_shared<GraphWater>();
  water->authoring = std::move(authoring);
  water->data = std::move(*resolved);
  GraphNodeResult result;
  result.outputs["field"] = std::move(field);
  result.outputs["water"] = std::shared_ptr<const GraphWater>(std::move(water));
  return result;
}
void requireMatchingWaterGrid(const GraphWater &water,
                              const HeightField &field) {
  const auto &coverage = water.data.resolvedCoverage;
  if (coverage &&
      (coverage->cellsX != field.cellsX || coverage->cellsZ != field.cellsZ ||
       coverage->size.x != field.size.x || coverage->size.y != field.size.y)) {
    throw std::invalid_argument(
        "Water authoring input does not match the terrain grid");
  }
}
} // namespace

std::optional<GraphNodeResult>
evaluateTerrainWaterEdit(const nlohmann::json &parameters,
                         const GraphValues &connected, std::stop_token stop) {
  const auto field = std::get<GraphField>(connected.at("field"));
  const auto water =
      std::get<std::shared_ptr<const GraphWater>>(connected.at("water"));
  requireMatchingWaterGrid(*water, *field);
  const auto excluded =
      parameters.at("excluded_ids").get<std::vector<std::string>>();
  auto authored = editTerrainWaterAuthoring(water->authoring, excluded,
                                            parameters.at("overrides"));
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  return publishWater(field, std::move(authored), stop);
}

std::optional<GraphNodeResult>
evaluateTerrainBasinWater(const TerrainGraphNode &node,
                          const nlohmann::json &parameters,
                          const GraphValues &connected, std::stop_token stop) {
  const auto drainage =
      std::get<std::shared_ptr<const GraphDrainage>>(connected.at("drainage"));
  const auto &field = drainage->source;
  const TerrainWaterProposalSettings settings{
      .minimumDepth = parameters.at("minimum_depth").get<float>(),
      .minimumArea = parameters.at("minimum_area").get<float>(),
      .maximumBodies = parameters.at("maximum_bodies").get<std::size_t>(),
      .levelOffset = parameters.at("level_offset").get<float>()};
  auto proposed =
      proposeTerrainBasinWater(*field, drainage->data, node.id, settings, stop);
  if (!proposed || stop.stop_requested()) {
    return std::nullopt;
  }
  auto authored = proposed->water;
  if (const auto found = connected.find("water"); found != connected.end()) {
    const auto existing =
        std::get<std::shared_ptr<const GraphWater>>(found->second);
    requireMatchingWaterGrid(*existing, *field);
    if (existing->data.carvedHeights != field->heights) {
      throw std::invalid_argument(
          "Existing water does not match the drainage source heights");
    }
    authored = existing->authoring;
    for (auto &body : proposed->water.bodies) {
      if (std::ranges::any_of(authored.bodies, [&](const auto &entry) {
            return entry.id == body.id;
          })) {
        throw std::invalid_argument("Duplicate proposed water body ID: " +
                                    body.id);
      }
      authored.bodies.push_back(std::move(body));
    }
  }
  auto result = publishWater(field, std::move(authored), stop);
  if (result) {
    result->status = "Proposed " +
                     std::to_string(proposed->water.bodies.size()) +
                     " lakes from " + std::to_string(proposed->eligibleBodies) +
                     " eligible closed basins.";
    if (proposed->truncated) {
      result->warnings.push_back("Water proposal body budget reached; raise "
                                 "Maximum bodies to retain more basins.");
    }
  }
  return result;
}
} // namespace demi::runtime::terrain_graph_detail
