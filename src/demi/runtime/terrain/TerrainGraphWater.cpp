#include "demi/runtime/terrain/TerrainGraphWater.h"
#include "demi/runtime/terrain/TerrainDrainageRoute.h"
#include "demi/runtime/terrain/TerrainWaterAuthoring.h"
#include "demi/runtime/terrain/TerrainWaterFlow.h"
#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime::terrain_graph_detail {
namespace {
GraphNodeResult unchangedRoute(GraphField source,
                               std::shared_ptr<const GraphWater> existing,
                               std::string status, bool warning) {
  if (!existing) {
    auto water = std::make_shared<GraphWater>();
    water->authoring.authored = true;
    water->data.carvedHeights = source->heights;
    existing = std::move(water);
  }
  GraphNodeResult result;
  result.outputs["field"] = std::move(source);
  result.outputs["water"] = std::move(existing);
  result.status = std::move(status);
  if (warning) {
    result.warnings.push_back(result.status);
  }
  return result;
}

std::string endpointDescription(TerrainDrainageRouteEndpoint endpoint) {
  switch (endpoint) {
  case TerrainDrainageRouteEndpoint::SeaLevel:
    return "sea level";
  case TerrainDrainageRouteEndpoint::FieldEdge:
    return "field edge";
  case TerrainDrainageRouteEndpoint::ClosedBasin:
    return "closed basin; no spill route was invented";
  case TerrainDrainageRouteEndpoint::BlockedSurface:
    return "blocked surface; drainage guidance would climb or cannot continue";
  }
  return "unknown endpoint";
}
} // namespace

std::optional<GraphNodeResult>
evaluateTerrainGraphWater(const TerrainGraphNode &node,
                          const nlohmann::json &values,
                          const GraphValues &connected, std::stop_token stop) {
  const auto source = std::get<GraphField>(connected.at("field"));
  auto water = std::make_shared<GraphWater>();
  water->authoring.authored = true;
  if (const auto upstream = connected.find("water");
      upstream != connected.end()) {
    const auto previous =
        std::get<std::shared_ptr<const GraphWater>>(upstream->second);
    if (previous->data.carvedHeights != source->heights) {
      throw std::invalid_argument(
          "Connected water data does not match the input field");
    }
    water->authoring = previous->authoring;
  }
  auto body = parseTerrainWaterBodyParameters(values, node.id);
  if (std::ranges::any_of(water->authoring.bodies, [&](const auto &entry) {
        return entry.id == body.id;
      })) {
    throw std::invalid_argument("Duplicate water body ID: " + body.id);
  }
  TerrainWaterAuthoring nextBody;
  nextBody.authored = true;
  nextBody.bodies.push_back(body);
  auto carved = carveTerrainWater(*source, nextBody, nullptr, stop);
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (!carved || carved->dropped) {
    throw std::invalid_argument(
        "Water body could not be placed on this surface");
  }
  auto field = std::make_shared<HeightField>(*source);
  field->heights = std::move(carved->carvedHeights);
  if (!TerrainGenerator::recomputeTerrainNormals(*field, stop)) {
    return std::nullopt;
  }
  water->authoring.bodies.push_back(std::move(body));
  auto resolved =
      refreshTerrainWaterResult(*field, water->authoring, field->heights, stop);
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (!resolved || resolved->dropped) {
    throw std::invalid_argument("Combined water bodies could not be resolved");
  }
  water->data = std::move(*resolved);
  GraphNodeResult result;
  result.outputs["field"] = GraphField(std::move(field));
  result.outputs["water"] = std::shared_ptr<const GraphWater>(std::move(water));
  return result;
}

std::optional<GraphNodeResult>
evaluateTerrainRiverRoute(const TerrainGraphNode &node,
                          const nlohmann::json &parameters,
                          const GraphValues &connected, std::stop_token stop) {
  const auto drainage =
      std::get<std::shared_ptr<const GraphDrainage>>(connected.at("drainage"));
  for (const auto *name : {"channel_width", "channel_depth", "water_depth"}) {
    const double value = parameters.at(name).get<double>();
    const float native = float(value);
    if (!std::isfinite(native) || native <= 0) {
      throw std::invalid_argument(
          std::string("River ") + name +
          " must be finite and positive in native units");
    }
  }
  if (parameters.at("water_depth").get<double>() >
      parameters.at("channel_depth").get<double>()) {
    throw std::invalid_argument("River water depth exceeds channel depth");
  }
  const auto &field = *drainage->source;
  const double sourceX = parameters.at("source_x").get<double>();
  const double sourceZ = parameters.at("source_z").get<double>();
  if (!std::isfinite(sourceX) || !std::isfinite(sourceZ) || sourceX < 0 ||
      sourceZ < 0 || sourceX > field.size.x || sourceZ > field.size.y) {
    throw std::invalid_argument(
        "River source must lie inside the terrain field");
  }
  const int x = int(std::lround(sourceX / field.size.x * field.cellsX));
  const int z = int(std::lround(sourceZ / field.size.y * field.cellsZ));
  auto route =
      traceTerrainDrainageRoute(field, drainage->data, field.index(x, z), stop);
  if (!route) {
    return std::nullopt;
  }
  std::shared_ptr<const GraphWater> existing;
  std::optional<TerrainWaterQueryContext> existingQueries;
  if (const auto upstream = connected.find("water");
      upstream != connected.end()) {
    existing = std::get<std::shared_ptr<const GraphWater>>(upstream->second);
    if (existing->data.carvedHeights != field.heights) {
      throw std::invalid_argument(
          "Connected water does not match the drainage source field");
    }
    if (!existing->authoring.bodies.empty()) {
      existingQueries = TerrainWaterQueryContext::build(
          existing->authoring, existing->data, nullptr, field.cellsX,
          field.cellsZ, field.size);
      if (!existingQueries) {
        throw std::invalid_argument("Connected water query context is invalid");
      }
    }
  }
  const double waterDepth = parameters.at("water_depth").get<double>();
  double bedFloor = -std::numeric_limits<double>::infinity();
  std::string status =
      "River route endpoint: " + endpointDescription(route->endpoint);
  bool warning =
      route->endpoint == TerrainDrainageRouteEndpoint::ClosedBasin ||
      route->endpoint == TerrainDrainageRouteEndpoint::BlockedSurface;
  bool joined = false;
  if (existingQueries) {
    for (std::size_t index = 0; index < route->points.size(); ++index) {
      if (stop.stop_requested()) {
        return std::nullopt;
      }
      const auto column = existingQueries->sample(route->points[index]);
      if (column && column->submerged) {
        bedFloor = double(column->surfaceHeight) - waterDepth;
        status = "River route joined water body: " + column->bodyId;
        if (index == 0) {
          status += "; source already wet";
        }
        warning = false;
        joined = true;
        route->points.resize(index + 1);
        route->samples.resize(index + 1);
        break;
      }
    }
  }
  if (!joined && route->endpoint == TerrainDrainageRouteEndpoint::SeaLevel) {
    bedFloor = double(drainage->data.seaLevel) - waterDepth;
  }
  if (route->points.size() < 2) {
    return unchangedRoute(
        drainage->source, existing,
        status + "; fewer than two route points, no new river published",
        warning);
  }
  TerrainGraphNode waterNode;
  waterNode.id = node.id;
  waterNode.type = "water";
  waterNode.parameters = {
      {"kind", "river"},
      {"river_surface_mode", "follow_bed"},
      {"river_width", parameters.at("channel_width")},
      {"river_depth", parameters.at("water_depth")},
      {"flow_speed", parameters.at("flow_speed")},
      {"radius", 0},
      {"river_path", nlohmann::json::array()},
  };
  const double channelDepth = parameters.at("channel_depth").get<double>();
  for (const auto point : route->points) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    // A constant receiving-water floor preserves the nonascending bed while
    // avoiding a submerged mouth below the joined surface (backwater).
    const double bed = std::max(double(point.y) - channelDepth, bedFloor);
    if (!std::isfinite(float(bed))) {
      throw std::invalid_argument(
          "River channel bed exceeds the native height range");
    }
    waterNode.parameters["river_path"].push_back({point.x, bed, point.z});
  }
  GraphValues waterInputs;
  waterInputs["field"] = drainage->source;
  if (existing) {
    waterInputs["water"] = existing;
  }
  try {
    auto result = evaluateTerrainGraphWater(
        waterNode, terrainGraphNodeParameters(waterNode), waterInputs, stop);
    if (result) {
      result->status = status;
      if (warning) {
        result->warnings.push_back(status);
      }
    }
    return result;
  } catch (const std::invalid_argument &error) {
    return unchangedRoute(drainage->source, existing,
                          status + "; river not published: " + error.what(),
                          true);
  }
}

} // namespace demi::runtime::terrain_graph_detail
