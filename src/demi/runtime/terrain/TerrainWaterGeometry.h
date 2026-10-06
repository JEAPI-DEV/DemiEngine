#pragma once

#include "demi/runtime/terrain/TerrainWater.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime::terrain_water_detail {

// Extend ownership onto ground-dry footprint samples so the depth plane, rather
// than a binary wet mask, locates the shore. A dry body never displaces a wet
// body's sample. Both geometry and queries use this field; render data is
// absent.
[[nodiscard]] WaterLevelField buildWaterGeometryField(
    Vec2 size, int cellsX, int cellsZ, const TerrainSamples<float> &ground,
    const TerrainWaterAuthoring &authoring, std::stop_token stop = {});

inline constexpr std::array<std::array<std::size_t, 3>, 2> waterCellTriangles{
    std::array<std::size_t, 3>{0, 2, 1}, std::array<std::size_t, 3>{1, 2, 3}};

inline std::array<double, 4> waterTriangleWeights(double u, double v) {
  return u + v <= 1.0
             ? std::array<double, 4>{1.0 - u - v, u, v, 0.0}
             : std::array<double, 4>{0.0, 1.0 - v, 1.0 - u, u + v - 1.0};
}

inline double waterOwnershipMask(bool covered, std::size_t owner,
                                 std::size_t body) {
  return covered && owner == body ? 1.0 : -1.0;
}

struct WaterGeometryVertex {
  double x = 0;
  double z = 0;
  double depth = 0;
  double coverage = 0;
};

// Two half-plane clips of a triangle have at most five distinct vertices.
struct WaterGeometryPolygon {
  std::array<WaterGeometryVertex, 6> vertices{};
  std::size_t count = 0;
};

inline bool sameWaterPosition(const WaterGeometryVertex &a,
                              const WaterGeometryVertex &b) {
  return a.x == b.x && a.z == b.z;
}

inline WaterGeometryVertex
waterBoundaryIntersection(WaterGeometryVertex a, WaterGeometryVertex b,
                          double WaterGeometryVertex::*field) {
  // Canonical edge direction makes opposite traversals produce exactly the
  // same crossing, including shared cell edges and the ground diagonal.
  if (a.x > b.x || (a.x == b.x && a.z > b.z))
    std::swap(a, b);
  if (a.*field == 0.0)
    return a;
  if (b.*field == 0.0)
    return b;
  const double t = (a.*field) / ((a.*field) - (b.*field));
  WaterGeometryVertex result{a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t,
                             a.depth + (b.depth - a.depth) * t,
                             a.coverage + (b.coverage - a.coverage) * t};
  result.*field = 0.0;
  return result;
}

inline WaterGeometryPolygon
clipWaterPolygon(const WaterGeometryPolygon &input,
                 double WaterGeometryVertex::*field) {
  WaterGeometryPolygon result;
  if (input.count == 0)
    return result;
  const auto append = [&](const WaterGeometryVertex &vertex) {
    if (result.count == 0 ||
        !sameWaterPosition(result.vertices[result.count - 1], vertex))
      result.vertices[result.count++] = vertex;
  };
  auto previous = input.vertices[input.count - 1];
  for (std::size_t i = 0; i < input.count; ++i) {
    const auto current = input.vertices[i];
    if ((previous.*field >= 0.0) != (current.*field >= 0.0))
      append(waterBoundaryIntersection(previous, current, field));
    if (current.*field >= 0.0)
      append(current);
    previous = current;
  }
  if (result.count > 1 &&
      sameWaterPosition(result.vertices[0], result.vertices[result.count - 1]))
    --result.count;
  return result;
}

inline WaterGeometryPolygon
clipWaterTriangle(const std::array<WaterGeometryVertex, 3> &triangle) {
  WaterGeometryPolygon polygon;
  bool hasCoverage = false;
  bool hasDepth = false;
  for (const auto &vertex : triangle) {
    if (!std::isfinite(vertex.depth))
      return {};
    hasCoverage = hasCoverage || vertex.coverage > 0.0;
    hasDepth = hasDepth || vertex.depth > 0.0;
    polygon.vertices[polygon.count++] = vertex;
  }
  if (!hasCoverage || !hasDepth)
    return {};
  polygon = clipWaterPolygon(polygon, &WaterGeometryVertex::depth);
  return clipWaterPolygon(polygon, &WaterGeometryVertex::coverage);
}

} // namespace demi::runtime::terrain_water_detail
