#include "demi/runtime/terrain/TerrainPolygon.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace {
bool samePoint(Vec2 left, Vec2 right) {
  return left.x == right.x && left.y == right.y;
}
std::size_t vertexCount(std::span<const Vec2> points) {
  return points.size() > 1 && samePoint(points.front(), points.back())
             ? points.size() - 1
             : points.size();
}
double orientation(Vec2 a, Vec2 b, Vec2 c) {
  return (double(b.x) - a.x) * (double(c.y) - a.y) -
         (double(b.y) - a.y) * (double(c.x) - a.x);
}
bool onSegment(Vec2 a, Vec2 b, Vec2 point) {
  return orientation(a, b, point) == 0 && point.x >= std::min(a.x, b.x) &&
         point.x <= std::max(a.x, b.x) && point.y >= std::min(a.y, b.y) &&
         point.y <= std::max(a.y, b.y);
}
bool intersects(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
  const auto abC = orientation(a, b, c);
  const auto abD = orientation(a, b, d);
  const auto cdA = orientation(c, d, a);
  const auto cdB = orientation(c, d, b);
  const bool crosses = ((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) &&
                       ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0));
  return crosses || onSegment(a, b, c) || onSegment(a, b, d) ||
         onSegment(c, d, a) || onSegment(c, d, b);
}
} // namespace

void validateTerrainPolygon(std::span<const Vec2> points,
                            std::stop_token stop) {
  const auto count = vertexCount(points);
  if (count < 3) {
    throw std::invalid_argument(
        "Terrain polygon requires at least three vertices");
  }
  double twiceArea = 0;
  for (std::size_t index = 0; index < count; ++index) {
    if (stop.stop_requested()) {
      return;
    }
    const auto point = points[index];
    const auto next = points[(index + 1) % count];
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      throw std::invalid_argument("Terrain polygon coordinates must be finite");
    }
    if (samePoint(point, next)) {
      throw std::invalid_argument("Terrain polygon has a zero-length edge");
    }
    const auto previous = points[(index + count - 1) % count];
    const double turnDot =
        (double(point.x) - previous.x) * (double(next.x) - point.x) +
        (double(point.y) - previous.y) * (double(next.y) - point.y);
    if (orientation(previous, point, next) == 0 && turnDot < 0) {
      throw std::invalid_argument("Terrain polygon backtracks along an edge");
    }
    twiceArea += double(point.x) * next.y - double(next.x) * point.y;
    for (std::size_t other = index + 1; other < count; ++other) {
      if ((other & 255) == 0 && stop.stop_requested()) {
        return;
      }
      const auto nextOther = (other + 1) % count;
      if (other == (index + 1) % count || nextOther == index) {
        continue;
      }
      if (intersects(point, next, points[other], points[nextOther])) {
        throw std::invalid_argument("Terrain polygon intersects itself");
      }
    }
  }
  if (twiceArea == 0 || !std::isfinite(twiceArea)) {
    throw std::invalid_argument("Terrain polygon has degenerate area");
  }
}

bool terrainPolygonContains(std::span<const Vec2> points, Vec2 position) {
  const auto count = vertexCount(points);
  bool inside = false;
  for (std::size_t index = 0; index < count; ++index) {
    const auto a = points[index];
    const auto b = points[(index + 1) % count];
    if (onSegment(a, b, position)) {
      return true;
    }
    if ((a.y > position.y) != (b.y > position.y)) {
      const double crossing = double(a.x) + (double(position.y) - a.y) *
                                                (double(b.x) - a.x) /
                                                (double(b.y) - a.y);
      if (position.x < crossing) {
        inside = !inside;
      }
    }
  }
  return inside;
}
} // namespace demi::runtime
