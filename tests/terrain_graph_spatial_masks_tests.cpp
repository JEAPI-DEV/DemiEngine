#include "demi/runtime/terrain/TerrainGraphSpatialMasks.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;
using namespace demi::runtime::terrain_graph_detail;

namespace {
GraphField sourceField() {
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.graph = defaultTerrainGraph();
  const auto field = executeTerrainGraph(recipe);
  assert(field);
  return std::make_shared<const HeightField>(*field);
}

std::shared_ptr<const GraphMask> polygon(const GraphField &source,
                                         const nlohmann::json &points,
                                         double falloff = 0) {
  const auto result = evaluateTerrainPolygonMask(
      {{"points", points}, {"falloff", falloff}}, source, {});
  assert(result);
  return std::get<std::shared_ptr<const GraphMask>>(result->outputs.at("mask"));
}

void filledBoundaryAndExteriorFalloff() {
  const auto source = sourceField();
  const nlohmann::json points = {{4, 4}, {12, 4}, {12, 12}, {4, 12}};
  const auto mask = polygon(source, points, 2);
  assert(mask->source == source);
  assert(mask->samples[source->index(8, 8)] == 1);
  assert(mask->samples[source->index(4, 8)] == 1);
  assert(mask->samples[source->index(4, 4)] == 1);
  assert(mask->samples[source->index(3, 8)] == 0.5F);
  assert(mask->samples[source->index(2, 8)] == 0);
  const auto hard = polygon(source, points);
  assert(hard->samples[source->index(3, 8)] == 0);
  auto reversed = points;
  std::reverse(reversed.begin(), reversed.end());
  const auto opposite = polygon(source, reversed, 2);
  for (std::size_t i = 0; i < mask->samples.size(); ++i) {
    assert(mask->samples[i] == opposite->samples[i]);
  }
}

void concavityRepeatedPointsAndSelfIntersections() {
  const auto source = sourceField();
  const auto concave = polygon(
      source,
      {{2, 2}, {12, 2}, {12, 6}, {6, 6}, {6, 12}, {2, 12}, {2, 12}, {2, 2}});
  assert(concave->samples[source->index(4, 10)] == 1);
  assert(concave->samples[source->index(10, 4)] == 1);
  assert(concave->samples[source->index(10, 10)] == 0);
  assert(concave->samples[source->index(6, 6)] == 1);
  // Signed polygon area is zero, but both even-odd lobes are filled.
  const auto crossed = polygon(source, {{2, 2}, {12, 12}, {2, 12}, {12, 2}});
  assert(crossed->samples[source->index(7, 3)] == 1);
  assert(crossed->samples[source->index(7, 11)] == 1);
  assert(crossed->samples[source->index(3, 7)] == 0);
  assert(crossed->samples[source->index(7, 7)] == 1);
}

void validationAndCancellation() {
  const auto source = sourceField();
  const auto rejects = [&](const nlohmann::json &points, double falloff) {
    bool parserRejected = false;
    try {
      validateTerrainSpatialMaskPoints(points, true);
    } catch (const std::exception &) {
      parserRejected = true;
    }
    if (falloff == 0) {
      assert(parserRejected);
    }
    bool rejected = false;
    try {
      (void)polygon(source, points, falloff);
    } catch (const std::exception &) {
      rejected = true;
    }
    assert(rejected);
  };
  rejects({{0, 0}, {1, 1}}, 0);
  rejects({{0, 0}, {1, 1}, {2, 2}, {0, 0}}, 0);
  rejects({{1, 1}, {1, 1}, {1, 1}}, 0);
  rejects({{0, 0}, {1e300, 1}, {0, 2}}, 0);
  rejects({{0, 0}, {"invalid", 1}, {0, 2}}, 0);
  rejects({{0, 0}, {1, 1}, {0, 2}}, -1);
  rejects({{0, 0}, {1, 1}, {0, 2}}, std::numeric_limits<double>::infinity());
  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!evaluateTerrainPolygonMask(
      {{"points", {{0, 0}, {12, 0}, {0, 12}}}, {"falloff", 2}}, source,
      cancelled.get_token()));
}
} // namespace

int main() {
  filledBoundaryAndExteriorFalloff();
  concavityRepeatedPointsAndSelfIntersections();
  validationAndCancellation();
  std::cout << "Terrain graph spatial masks passed\n";
}
