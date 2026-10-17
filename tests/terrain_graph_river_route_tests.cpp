#include "demi/runtime/terrain/TerrainDrainageRoute.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "demi/runtime/terrain/TerrainGraphWater.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainPreset.h"
#include "demi/runtime/terrain/TerrainWaterQueries.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;
using namespace demi::runtime::terrain_graph_detail;

namespace {

GraphField slopeField() {
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").heightVariation = 0;
  auto field = TerrainGenerator::generate(recipe);
  assert(field);
  for (int z = 0; z <= field->cellsZ; ++z) {
    for (int x = 0; x <= field->cellsX; ++x) {
      field->heights.set(field->index(x, z), 12.0F - float(z));
    }
  }
  assert(TerrainGenerator::recomputeTerrainNormals(*field));
  return std::make_shared<const HeightField>(std::move(*field));
}

std::shared_ptr<const GraphDrainage> drainageFor(GraphField field,
                                                 float seaLevel) {
  TerrainRecipe recipe;
  recipe.size = field->size;
  recipe.cellsX = field->cellsX;
  recipe.cellsZ = field->cellsZ;
  TerrainDrainageSettings settings;
  settings.seaLevel = seaLevel;
  const auto computed = computeTerrainDrainage(*field, recipe, settings);
  assert(computed);
  auto drainage = std::make_shared<GraphDrainage>();
  drainage->source = std::move(field);
  drainage->data = *computed;
  return drainage;
}

TerrainGraphNode routeNode(double x = 8, double z = 4) {
  TerrainGraphNode node;
  node.id = "routed_river";
  node.type = "river_route";
  node.parameters = {{"source_x", x},
                     {"source_z", z},
                     {"channel_width", 4},
                     {"channel_depth", 2},
                     {"water_depth", 1}};
  return node;
}

std::optional<GraphNodeResult> evaluateRoute(
    const TerrainGraphNode &node, std::shared_ptr<const GraphDrainage> drainage,
    std::stop_token stop = {}, std::shared_ptr<const GraphWater> water = {}) {
  GraphValues connected;
  connected["drainage"] = std::move(drainage);
  if (water) {
    connected["water"] = std::move(water);
  }
  return evaluateTerrainRiverRoute(node, terrainGraphNodeParameters(node),
                                   connected, stop);
}

bool hasWarning(const GraphNodeResult &result, std::string_view fragment) {
  return std::ranges::any_of(result.warnings, [&](const auto &warning) {
    return warning.find(fragment) != std::string::npos;
  });
}

void routeCarvesAndPublishesAStableSlopedWaterBody() {
  const auto source = slopeField();
  const auto node = routeNode();
  const auto result = evaluateRoute(node, drainageFor(source, 0));
  assert(result);
  assert(result->status.find("sea level") != std::string::npos ||
         result->status.find("field edge") != std::string::npos);
  assert(result->warnings.empty());
  const auto field = std::get<GraphField>(result->outputs.at("field"));
  const auto water =
      std::get<std::shared_ptr<const GraphWater>>(result->outputs.at("water"));
  assert(water->authoring.bodies.size() == 1);
  assert(water->data.dropped == 0 && !water->data.surfaces.empty());
  const auto &body = water->authoring.bodies.front();
  assert(body.id == node.id && body.kind == TerrainWaterBody::River);
  assert(body.riverSurfaceMode == TerrainRiverSurfaceMode::FollowBed);
  assert(body.riverDepth == 1 && body.riverPath.size() >= 2);
  for (std::size_t index = 0; index < body.riverPath.size(); ++index) {
    const auto point = body.riverPath[index];
    const int x = int(std::lround(point.x));
    const int z = int(std::lround(point.z));
    const bool seaEndpoint =
        result->status.find("sea level") != std::string::npos;
    const float floor =
        seaEndpoint ? -1.0F : std::numeric_limits<float>::lowest();
    assert(std::abs(point.y - std::max(source->height(x, z) - 2, floor)) <
           1e-5F);
    if (index > 0) {
      assert(point.y <= body.riverPath[index - 1].y);
    }
  }
  assert(source->height(8, 4) == 8);
  const auto interior = body.riverPath[body.riverPath.size() / 2];
  const int interiorX = int(std::lround(interior.x));
  const int interiorZ = int(std::lround(interior.z));
  assert(field->height(interiorX, interiorZ) <
         source->height(interiorX, interiorZ));
  assert(std::abs(field->height(interiorX, interiorZ) - interior.y) < 1e-5F);
  const auto &surface = water->data.surfaces.front();
  assert(!surface.vertices.empty());
  float lowest = surface.vertices.front().y;
  float highest = lowest;
  for (const auto vertex : surface.vertices) {
    lowest = std::min(lowest, vertex.y);
    highest = std::max(highest, vertex.y);
  }
  assert(highest > lowest + 1);
}

void terminalOrBlockedSourcesPublishNoInvalidWater() {
  const auto source = slopeField();
  const auto edge = evaluateRoute(routeNode(0, 0), drainageFor(source, 0));
  assert(edge && edge->status.find("field edge") != std::string::npos);
  assert(edge->warnings.empty());
  assert(std::get<GraphField>(edge->outputs.at("field")) == source);
  const auto empty =
      std::get<std::shared_ptr<const GraphWater>>(edge->outputs.at("water"));
  assert(empty->authoring.bodies.empty() && empty->data.surfaces.empty());

  auto guidance = std::make_shared<GraphDrainage>(*drainageFor(source, 0));
  guidance->data.downstream.set(source->index(8, 4), source->index(8, 3));
  guidance->data.downstream.set(source->index(8, 3), terrainNoDownstream);
  const auto blocked = evaluateRoute(routeNode(), guidance);
  assert(blocked && hasWarning(*blocked, "blocked surface"));
  assert(std::get<GraphField>(blocked->outputs.at("field")) == source);
  const auto blockedWater =
      std::get<std::shared_ptr<const GraphWater>>(blocked->outputs.at("water"));
  assert(blockedWater->authoring.bodies.empty());
  auto basin = std::make_shared<HeightField>(*source);
  for (int z = 0; z <= basin->cellsZ; ++z) {
    for (int x = 0; x <= basin->cellsX; ++x) {
      basin->heights.set(basin->index(x, z),
                         float((x - 8) * (x - 8) + (z - 8) * (z - 8)));
    }
  }
  const auto closed = evaluateRoute(routeNode(8, 8), drainageFor(basin, -1));
  assert(closed && hasWarning(*closed, "closed basin"));
  const auto closedWater =
      std::get<std::shared_ptr<const GraphWater>>(closed->outputs.at("water"));
  assert(closedWater->authoring.bodies.empty());
}

void existingWaterJoinsRetainBodiesAndClampTheRiverMouth() {
  for (const std::string kind : {"lake", "river"}) {
    TerrainGraphNode body;
    body.id = "receiving_" + kind;
    body.type = "water";
    body.parameters = {
        {"kind", kind},
        {"level", 0},
        {"center_x", 8},
        {"center_z", 13},
        {"radius", 5},
        {"river_width", 8},
        {"river_path", {{0, -3, 12}, {16, -3, 12}}},
    };
    GraphValues inputs;
    inputs["field"] = slopeField();
    const auto prepared = evaluateTerrainGraphWater(
        body, terrainGraphNodeParameters(body), inputs, {});
    assert(prepared);
    const auto source = std::get<GraphField>(prepared->outputs.at("field"));
    const auto existing = std::get<std::shared_ptr<const GraphWater>>(
        prepared->outputs.at("water"));
    // Receiving water sits above the drainage sea plane. Otherwise tracing
    // ends on a dry sea-level shoreline before reaching the first wet column.
    auto drainage = std::make_shared<GraphDrainage>(*drainageFor(source, -10));
    // Keep this fixture's route on the receiving body's interior centreline.
    for (int z = 4; z < source->cellsZ; ++z) {
      drainage->data.downstream.set(source->index(8, z),
                                    source->index(8, z + 1));
    }
    drainage->data.downstream.set(source->index(8, source->cellsZ),
                                  terrainNoDownstream);
    const auto result = evaluateRoute(routeNode(), drainage, {}, existing);
    assert(result && result->warnings.empty());
    const std::string expectedStatus =
        "River route joined water body: " + body.id;
    if (result->status != expectedStatus) {
      std::cerr << "Expected " << expectedStatus << ", observed "
                << result->status << '\n';
    }
    assert(result->status == expectedStatus);
    const auto combined = std::get<std::shared_ptr<const GraphWater>>(
        result->outputs.at("water"));
    assert(combined->authoring.bodies.size() == 2);
    assert(combined->authoring.bodies.front().id == body.id);
    const auto &path = combined->authoring.bodies.back().riverPath;
    assert(path.size() >= 2);
    for (std::size_t index = 0; index < path.size(); ++index) {
      assert(path[index].y + 1 >= -1e-5F);
      if (index > 0) {
        assert(path[index].y <= path[index - 1].y);
      }
    }
    assert(std::abs(path.back().y + 1) < 1e-5F);
    const auto alreadyWet =
        evaluateRoute(routeNode(8, 13), drainage, {}, existing);
    assert(alreadyWet && alreadyWet->warnings.empty());
    assert(std::get<GraphField>(alreadyWet->outputs.at("field")) == source);
    assert(std::get<std::shared_ptr<const GraphWater>>(
               alreadyWet->outputs.at("water")) == existing);
    const auto shortRoute =
        evaluateRoute(routeNode(0, 0), drainage, {}, existing);
    assert(shortRoute);
    assert(std::get<std::shared_ptr<const GraphWater>>(
               shortRoute->outputs.at("water")) == existing);
  }
}

void routeCoordinatesAndCancellationRemainExplicit() {
  const auto drainage = drainageFor(slopeField(), 0);
  for (const auto x : {-1.0, 17.0, std::numeric_limits<double>::infinity()}) {
    bool rejected = false;
    try {
      (void)evaluateRoute(routeNode(x), drainage);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  std::stop_source cancellation;
  cancellation.request_stop();
  assert(!evaluateRoute(routeNode(), drainage, cancellation.get_token()));
}

void graphPublishesShortRouteWarningsWithoutInvalidWater() {
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  recipe.graph = nlohmann::json::parse(R"({
    "format_version": 1,
    "output": "output",
    "nodes": [
      {"id":"source", "type":"constant", "parameters":{"height":0}},
      {"id":"drainage", "type":"drainage", "parameters":{"sea_level":0}},
      {"id":"route", "type":"river_route", "parameters":{"source_x":8,"source_z":8}},
      {"id":"output", "type":"output"}
    ],
    "links": [
      {"id":"source_drainage", "from":{"node":"source","port":"field"}, "to":{"node":"drainage","port":"field"}},
      {"id":"drainage_route", "from":{"node":"drainage","port":"drainage"}, "to":{"node":"route","port":"drainage"}},
      {"id":"route_field", "from":{"node":"route","port":"field"}, "to":{"node":"output","port":"field"}},
      {"id":"route_water", "from":{"node":"route","port":"water"}, "to":{"node":"output","port":"water"}}
    ]
  })");
  const auto generated = executeTerrainGraph(recipe);
  assert(generated && generated->graphArtifacts->water.bodies.empty());
  assert(generated->graphArtifacts->warnings.empty());
  const auto run = std::ranges::find(generated->graphArtifacts->nodes, "route",
                                     &TerrainGraphNodeRun::id);
  assert(run != generated->graphArtifacts->nodes.end());
  assert(run->status.find("sea level") != std::string::npos);
  TerrainGraphInputs retained;
  retained.previousCache = generated->graphArtifacts->cache;
  const auto cached = executeTerrainGraph(recipe, retained);
  assert(cached && cached->heights == generated->heights);
  assert(cached->graphArtifacts->warnings ==
         generated->graphArtifacts->warnings);
  const auto cachedRun = std::ranges::find(cached->graphArtifacts->nodes,
                                           "route", &TerrainGraphNodeRun::id);
  assert(cachedRun != cached->graphArtifacts->nodes.end());
  assert(cachedRun->cached && cachedRun->status == run->status);
}

void chunkPartitionsPreserveRoutedRiverAndSeamQueries() {
  const auto preset = loadTerrainPreset({}, "builtin://terrain/drainage-river");
  assert(preset);
  auto recipe = TerrainRecipe::parse(terrainPresetFragment(*preset));
  recipe.chunkCells = 7;
  const auto first = executeTerrainGraph(recipe);
  recipe.chunkCells = 16;
  const auto second = executeTerrainGraph(recipe);
  assert(first && second);
  assert(first->chunks.size() != second->chunks.size());
  assert(first->heights == second->heights);
  assert(first->baseHeights == second->baseHeights);
  assert(first->biomeIndices == second->biomeIndices);

  const auto firstDrainage =
      drainageFor(std::make_shared<const HeightField>(*first), 0);
  const auto secondDrainage =
      drainageFor(std::make_shared<const HeightField>(*second), 0);
  assert(firstDrainage->data.downstream == secondDrainage->data.downstream);
  assert(firstDrainage->data.basin == secondDrainage->data.basin);
  assert(firstDrainage->data.flow == secondDrainage->data.flow);
  const auto source = first->index(32, 16);
  const auto firstRoute =
      traceTerrainDrainageRoute(*first, firstDrainage->data, source);
  const auto secondRoute =
      traceTerrainDrainageRoute(*second, secondDrainage->data, source);
  assert(firstRoute && secondRoute);
  assert(firstRoute->samples == secondRoute->samples);
  assert(firstRoute->endpoint == secondRoute->endpoint);

  const auto &firstWater = first->graphArtifacts->water;
  const auto &secondWater = second->graphArtifacts->water;
  const auto firstRiver = std::ranges::find(
      firstWater.bodies, std::string("river"), &TerrainWaterBodySpec::id);
  const auto secondRiver = std::ranges::find(
      secondWater.bodies, std::string("river"), &TerrainWaterBodySpec::id);
  assert(firstRiver != firstWater.bodies.end());
  assert(secondRiver != secondWater.bodies.end());
  assert(firstRiver->riverPath.size() > 1);
  assert(firstRiver->riverPath.size() == secondRiver->riverPath.size());
  for (std::size_t point = 0; point < firstRiver->riverPath.size(); ++point) {
    const auto a = firstRiver->riverPath[point];
    const auto b = secondRiver->riverPath[point];
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
  }
  TerrainMasks firstMasks;
  TerrainMasks secondMasks;
  firstMasks.flow = firstDrainage->data.flow;
  secondMasks.flow = secondDrainage->data.flow;
  const auto firstQueries = TerrainWaterQueryContext::build(
      firstWater, *first->graphArtifacts->waterResult, &firstMasks,
      first->cellsX, first->cellsZ, first->size);
  const auto secondQueries = TerrainWaterQueryContext::build(
      secondWater, *second->graphArtifacts->waterResult, &secondMasks,
      second->cellsX, second->cellsZ, second->size);
  assert(firstQueries && secondQueries);
  std::size_t riverColumns = 0;
  const auto compareColumn = [&](Vec2 position) {
    const auto a = firstQueries->sample({position.x, 0, position.y});
    const auto b = secondQueries->sample({position.x, 0, position.y});
    assert(a && b);
    assert(a->submerged == b->submerged && a->underwater == b->underwater);
    assert(a->bodyId == b->bodyId);
    assert(a->flowVelocity.x == b->flowVelocity.x &&
           a->flowVelocity.y == b->flowVelocity.y &&
           a->flowVelocity.z == b->flowVelocity.z);
    assert(a->groundHeight == b->groundHeight &&
           a->surfaceHeight == b->surfaceHeight);
    assert(a->depth == b->depth && a->flowSpeed == b->flowSpeed);
    assert(a->normal.x == b->normal.x && a->normal.y == b->normal.y &&
           a->normal.z == b->normal.z);
    if (a->submerged && a->bodyId == "river") {
      ++riverColumns;
    }
  };
  // Every grid column covers all partition seams; half-sample probes on each
  // chunk's four edges also exercise interpolation at odd/partial boundaries.
  for (int z = 0; z <= first->cellsZ; ++z) {
    for (int x = 0; x <= first->cellsX; ++x) {
      compareColumn(first->position(x, z));
    }
  }
  for (const auto *field : {&*first, &*second}) {
    for (const auto &chunk : field->chunks) {
      for (int offset = 0; offset < chunk.cellsZ; ++offset) {
        const auto low =
            field->position(chunk.firstCellX, chunk.firstCellZ + offset);
        const auto high = field->position(chunk.firstCellX + chunk.cellsX,
                                          chunk.firstCellZ + offset);
        const float halfStep = field->size.y / field->cellsZ * .5F;
        compareColumn({low.x, low.y + halfStep});
        compareColumn({high.x, high.y + halfStep});
      }
      for (int offset = 0; offset < chunk.cellsX; ++offset) {
        const auto low =
            field->position(chunk.firstCellX + offset, chunk.firstCellZ);
        const auto high = field->position(chunk.firstCellX + offset,
                                          chunk.firstCellZ + chunk.cellsZ);
        const float halfStep = field->size.x / field->cellsX * .5F;
        compareColumn({low.x + halfStep, low.y});
        compareColumn({high.x + halfStep, high.y});
      }
    }
  }
  assert(riverColumns > 0);
}

void parserMetadataAndAuthoredRiverModeShareTheContract() {
  const auto *definition = terrainGraphNodeDefinition("river_route");
  assert(definition && definition->inputs.size() == 2);
  assert(definition->inputs[1].name == "water" &&
         !definition->inputs[1].required);
  assert(definition->inputs.front().required);
  assert(definition->inputs.front().kind == TerrainGraphValueKind::Drainage);
  auto graph = defaultTerrainGraph();
  auto node = routeNode();
  graph["nodes"].push_back(
      {{"id", node.id}, {"type", node.type}, {"parameters", node.parameters}});
  assert(TerrainGraph::parse(graph).node(node.id));
  for (const auto parameters :
       {nlohmann::json{{"channel_width", 0}},
        nlohmann::json{{"channel_depth", -1}},
        nlohmann::json{{"channel_depth", 1}, {"water_depth", 2}},
        nlohmann::json{{"water_depth", 0}},
        nlohmann::json{{"source_x", 1e100}}}) {
    graph["nodes"].back()["parameters"] = parameters;
    bool rejected = false;
    try {
      (void)TerrainGraph::parse(graph);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  graph["nodes"].back()["parameters"] = node.parameters;
  graph["links"][0]["from"]["node"] = node.id;
  bool missingDrainageRejected = false;
  try {
    (void)TerrainGraph::parse(graph);
  } catch (const std::invalid_argument &error) {
    missingDrainageRejected =
        std::string(error.what()).find("Drainage") != std::string::npos;
  }
  assert(missingDrainageRejected);

  TerrainGraphNode water;
  water.id = "authored_river";
  water.type = "water";
  water.parameters = {{"kind", "river"},
                      {"level", 99},
                      {"river_surface_mode", "follow_bed"},
                      {"river_depth", 0.5},
                      {"river_width", 4},
                      {"river_path", {{8, 6, 4}, {8, -2, 12}}}};
  GraphValues connected;
  connected["field"] = slopeField();
  const auto result = evaluateTerrainGraphWater(
      water, terrainGraphNodeParameters(water), connected, {});
  assert(result);
  const auto resolved =
      std::get<std::shared_ptr<const GraphWater>>(result->outputs.at("water"));
  assert(resolved->authoring.bodies.front().riverSurfaceMode ==
         TerrainRiverSurfaceMode::FollowBed);
  assert(resolved->authoring.bodies.front().riverDepth == 0.5F);
  for (const auto &surface : resolved->data.surfaces) {
    for (const auto vertex : surface.vertices) {
      assert(vertex.y < 10);
    }
  }
}

} // namespace

int main() {
  routeCarvesAndPublishesAStableSlopedWaterBody();
  terminalOrBlockedSourcesPublishNoInvalidWater();
  existingWaterJoinsRetainBodiesAndClampTheRiverMouth();
  routeCoordinatesAndCancellationRemainExplicit();
  graphPublishesShortRouteWarningsWithoutInvalidWater();
  chunkPartitionsPreserveRoutedRiverAndSeamQueries();
  parserMetadataAndAuthoredRiverModeShareTheContract();
  std::cout << "Terrain graph river route checks passed\n";
}
