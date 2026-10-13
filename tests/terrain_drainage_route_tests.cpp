#include "demi/runtime/terrain/TerrainDrainageRoute.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;

namespace {
struct Fixture {
  HeightField field;
  TerrainDrainage drainage;

  Fixture() {
    field.size = {8, 4};
    field.cellsX = field.cellsZ = 4;
    field.heights.resize(25);
    drainage.downstream.resize(25);
    drainage.basin.resize(25);
    for (std::size_t sample = 0; sample < 25; ++sample) {
      field.heights.set(sample, 20);
      drainage.downstream.set(sample, terrainNoDownstream);
    }
  }

  std::size_t set(int x, int z, float height) {
    const auto sample = field.index(x, z);
    field.heights.set(sample, height);
    return sample;
  }
};

void seaEdgeAndBasinEndpoints() {
  Fixture fixture;
  const auto source = fixture.set(2, 2, 3);
  const auto middle = fixture.set(1, 2, 2);
  const auto edge = fixture.set(0, 2, 1);
  fixture.drainage.downstream.set(source, middle);
  fixture.drainage.downstream.set(middle, edge);
  auto route =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route && route->endpoint == TerrainDrainageRouteEndpoint::FieldEdge);
  assert((route->samples == std::vector<std::size_t>{source, middle, edge}));
  assert(route->points[0].x == 4 && route->points[0].z == 2);
  fixture.drainage.seaLevel = 2;
  route = traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route && route->endpoint == TerrainDrainageRouteEndpoint::SeaLevel);
  assert(route->samples.back() == middle);
  fixture.drainage.seaLevel = 0;
  fixture.drainage.basinCount = 1;
  fixture.drainage.basin.set(middle, 1);
  fixture.set(0, 2, 4);
  route = traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route && route->endpoint == TerrainDrainageRouteEndpoint::ClosedBasin);
  assert(route->basin == 1 && route->samples.back() == middle);
}

void oppositeDiagonalUsesDescendingCardinalEdges() {
  Fixture fixture;
  const auto source = fixture.set(1, 1, 5);
  const auto target = fixture.set(2, 2, 2);
  const auto lower = fixture.set(2, 1, 3);
  fixture.set(1, 2, 4);
  fixture.drainage.downstream.set(source, target);
  const auto route =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route);
  assert((route->samples == std::vector<std::size_t>{source, lower, target}));
  for (std::size_t point = 1; point < route->points.size(); ++point) {
    assert(route->points[point].y <= route->points[point - 1].y);
  }
  const auto repeated =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(repeated && repeated->samples == route->samples);
  fixture.set(2, 1, 8);
  fixture.set(1, 2, 9);
  const auto blocked =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(blocked &&
         blocked->endpoint == TerrainDrainageRouteEndpoint::BlockedSurface);
  assert(blocked->samples.size() == 1);
}

void cardinalDetourResumesFutureGuidanceWithoutRevisiting() {
  Fixture fixture;
  const auto source = fixture.set(1, 1, 5);
  const auto target = fixture.set(2, 2, 2);
  const auto cardinal = fixture.set(2, 1, 3);
  const auto last = fixture.set(3, 1, 1);
  fixture.drainage.downstream.set(source, target);
  fixture.drainage.downstream.set(target, cardinal);
  fixture.drainage.downstream.set(cardinal, last);
  const auto route =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route);
  assert((route->samples == std::vector<std::size_t>{source, cardinal, last}));
}

void trueMeshDiagonalAndUphillGuidance() {
  Fixture fixture;
  const auto source = fixture.set(1, 2, 4);
  const auto target = fixture.set(2, 1, 2);
  fixture.drainage.downstream.set(source, target);
  auto route =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route && route->samples.size() == 2);
  fixture.set(2, 1, 8);
  route = traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route &&
         route->endpoint == TerrainDrainageRouteEndpoint::BlockedSurface);
  assert(route->samples.size() == 1);
}

void isolatedDepressionBelowSeaStopsAtItsOwnSink() {
  Fixture fixture;
  const auto source = fixture.set(2, 2, -1);
  const auto sink = fixture.set(1, 2, -2);
  fixture.drainage.basinCount = 1;
  fixture.drainage.basin.set(source, 1);
  fixture.drainage.basin.set(sink, 1);
  fixture.drainage.downstream.set(source, sink);
  const auto route =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
  assert(route && route->endpoint == TerrainDrainageRouteEndpoint::ClosedBasin);
  assert((route->samples == std::vector<std::size_t>{source, sink}));
  assert(route->basin == 1);
  const auto atSink =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, sink);
  assert(atSink && atSink->samples.size() == 1);
  assert(atSink->endpoint == TerrainDrainageRouteEndpoint::ClosedBasin);
  const auto seaSource = fixture.set(3, 3, -1);
  const auto sea =
      traceTerrainDrainageRoute(fixture.field, fixture.drainage, seaSource);
  assert(sea && sea->samples.size() == 1);
  assert(sea->endpoint == TerrainDrainageRouteEndpoint::SeaLevel);
}

void corruptionAndCancellation() {
  const auto rejected = [](const Fixture &fixture, std::size_t source) {
    bool failed = false;
    try {
      (void)traceTerrainDrainageRoute(fixture.field, fixture.drainage, source);
    } catch (const std::invalid_argument &) {
      failed = true;
    }
    assert(failed);
  };
  Fixture fixture;
  const auto source = fixture.field.index(2, 2);
  fixture.drainage.downstream.set(source, 100);
  rejected(fixture, source);
  fixture.drainage.downstream.set(source, fixture.field.index(0, 0));
  rejected(fixture, source);
  fixture.drainage.downstream.set(source, fixture.field.index(1, 2));
  fixture.drainage.downstream.set(fixture.field.index(1, 2), source);
  rejected(fixture, source);
  fixture.drainage.downstream.set(source, terrainNoDownstream);
  fixture.drainage.basin.resize(1);
  rejected(fixture, source);
  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!traceTerrainDrainageRoute(fixture.field, fixture.drainage, source,
                                    cancelled.get_token()));
}
} // namespace

int main() {
  seaEdgeAndBasinEndpoints();
  oppositeDiagonalUsesDescendingCardinalEdges();
  cardinalDetourResumesFutureGuidanceWithoutRevisiting();
  trueMeshDiagonalAndUphillGuidance();
  isolatedDepressionBelowSeaStopsAtItsOwnSink();
  corruptionAndCancellation();
  std::cout << "Terrain drainage route checks passed\n";
}
