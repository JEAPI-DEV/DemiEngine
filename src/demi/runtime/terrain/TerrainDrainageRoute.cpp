#include "demi/runtime/terrain/TerrainDrainageRoute.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace demi::runtime {
namespace {

struct SampleGrid {
  std::size_t columns;
  std::size_t count;

  std::pair<int, int> coordinates(std::size_t sample) const {
    return {int(sample % columns), int(sample / columns)};
  }
};

SampleGrid validateGrid(const HeightField &field,
                        const TerrainDrainage &drainage, std::size_t source) {
  if (field.cellsX <= 0 || field.cellsZ <= 0 || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || field.size.x <= 0 || field.size.y <= 0 ||
      !std::isfinite(drainage.seaLevel)) {
    throw std::invalid_argument(
        "Drainage route requires a finite positive grid");
  }
  const std::size_t columns = std::size_t(field.cellsX) + 1;
  const std::size_t rows = std::size_t(field.cellsZ) + 1;
  if (rows > std::numeric_limits<std::size_t>::max() / columns) {
    throw std::invalid_argument(
        "Drainage route grid exceeds addressable samples");
  }
  const std::size_t count = columns * rows;
  if (field.heights.size() != count || drainage.downstream.size() != count ||
      drainage.basin.size() != count || source >= count) {
    throw std::invalid_argument(
        "Drainage route storage does not match its grid");
  }
  return {columns, count};
}

void validateLink(const SampleGrid &grid, std::size_t from, std::size_t to) {
  if (to >= grid.count) {
    throw std::invalid_argument(
        "Drainage route has an invalid downstream index");
  }
  const auto [fromX, fromZ] = grid.coordinates(from);
  const auto [toX, toZ] = grid.coordinates(to);
  if (from == to || std::abs(fromX - toX) > 1 || std::abs(fromZ - toZ) > 1) {
    throw std::invalid_argument(
        "Drainage route downstream links must be adjacent");
  }
}

std::optional<TerrainDrainageRouteEndpoint>
endpointAt(const HeightField &field, const TerrainDrainage &drainage,
           const SampleGrid &grid, std::size_t sample) {
  if (field.heights[sample] <= drainage.seaLevel &&
      drainage.basin[sample] == 0) {
    return TerrainDrainageRouteEndpoint::SeaLevel;
  }
  const auto [x, z] = grid.coordinates(sample);
  if (x == 0 || z == 0 || x == field.cellsX || z == field.cellsZ) {
    return TerrainDrainageRouteEndpoint::FieldEdge;
  }
  return std::nullopt;
}

} // namespace

std::optional<TerrainDrainageRoute>
traceTerrainDrainageRoute(const HeightField &field,
                          const TerrainDrainage &drainage,
                          std::size_t sourceSample, std::stop_token stop) {
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  const auto grid = validateGrid(field, drainage, sourceSample);
  // Validate guidance before accepting an early endpoint. Corrupt links cannot
  // silently become a plausible route simply because its source touches sea.
  std::unordered_set<std::size_t> guidance;
  auto sample = sourceSample;
  while (sample != terrainNoDownstream) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    if (!guidance.insert(sample).second) {
      throw std::invalid_argument(
          "Drainage route downstream chain contains a cycle");
    }
    if (!std::isfinite(field.heights[sample]) ||
        drainage.basin[sample] > drainage.basinCount) {
      throw std::invalid_argument(
          "Drainage route has invalid elevation or basin data");
    }
    const auto next = drainage.downstream[sample];
    if (next != terrainNoDownstream) {
      validateLink(grid, sample, next);
    }
    sample = next;
  }

  TerrainDrainageRoute route;
  std::unordered_set<std::size_t> emitted;
  const auto append = [&](std::size_t current) {
    const auto [x, z] = grid.coordinates(current);
    const auto position = field.position(x, z);
    route.samples.push_back(current);
    route.points.push_back({position.x, field.heights[current], position.y});
    emitted.insert(current);
  };
  sample = sourceSample;
  append(sample);
  while (true) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    if (const auto endpoint = endpointAt(field, drainage, grid, sample)) {
      route.endpoint = *endpoint;
      route.basin = drainage.basin[sample];
      return route;
    }
    const auto next = drainage.downstream[sample];
    if (next == terrainNoDownstream ||
        field.heights[next] > field.heights[sample]) {
      route.basin = drainage.basin[sample];
      route.endpoint = route.basin != 0
                           ? TerrainDrainageRouteEndpoint::ClosedBasin
                           : TerrainDrainageRouteEndpoint::BlockedSurface;
      return route;
    }
    if (emitted.contains(next)) {
      route.endpoint = TerrainDrainageRouteEndpoint::BlockedSurface;
      route.basin = drainage.basin[sample];
      return route;
    }
    const auto [x, z] = grid.coordinates(sample);
    const auto [nextX, nextZ] = grid.coordinates(next);
    // Canonical triangles are {a,c,b}/{b,c,d}: only the opposite-sign diagonal
    // is a mesh edge. The a-d diagonal crosses the interior edge and can crest
    // above both endpoints, so follow two cardinal mesh edges instead.
    const bool crossesTriangleEdge = (nextX - x) * (nextZ - z) > 0;
    if (crossesTriangleEdge) {
      const std::size_t candidates[] = {field.index(nextX, z),
                                        field.index(x, nextZ)};
      auto intermediate = terrainNoDownstream;
      for (const auto candidate : candidates) {
        const float height = field.heights[candidate];
        if (!std::isfinite(height) ||
            drainage.basin[candidate] > drainage.basinCount) {
          throw std::invalid_argument(
              "Drainage route has a nonfinite intermediate elevation");
        }
        if (emitted.contains(candidate) || height > field.heights[sample] ||
            height < field.heights[next]) {
          continue;
        }
        if (intermediate == terrainNoDownstream ||
            height < field.heights[intermediate] ||
            (height == field.heights[intermediate] &&
             candidate < intermediate)) {
          intermediate = candidate;
        }
      }
      if (intermediate == terrainNoDownstream) {
        route.basin = drainage.basin[sample];
        return route;
      }
      append(intermediate);
      if (const auto endpoint =
              endpointAt(field, drainage, grid, intermediate)) {
        route.endpoint = *endpoint;
        route.basin = drainage.basin[intermediate];
        return route;
      }
      // Avoid revisiting a future guidance node after the cardinal detour.
      if (guidance.contains(intermediate)) {
        sample = intermediate;
        continue;
      }
    }
    append(next);
    sample = next;
  }
}

} // namespace demi::runtime
