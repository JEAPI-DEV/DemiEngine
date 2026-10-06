#include "demi/runtime/terrain/TerrainSurface.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime {
namespace {
struct Vector {
  double x, y, z;
};
Vector subtract(Vector a, Vector b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vector cross(Vector a, Vector b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(Vector a, Vector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

std::optional<double> triangleHit(Vector origin, Vector direction, Vector a,
                                  Vector b, Vector c) {
  const auto edge1 = subtract(b, a);
  const auto edge2 = subtract(c, a);
  const auto perpendicular = cross(direction, edge2);
  const double determinant = dot(edge1, perpendicular);
  if (determinant == 0)
    return std::nullopt;
  const auto offset = subtract(origin, a);
  const double u = dot(offset, perpendicular) / determinant;
  const auto secondPerpendicular = cross(offset, edge1);
  const double v = dot(direction, secondPerpendicular) / determinant;
  constexpr double edgeTolerance = 1e-7;
  if (u < -edgeTolerance || v < -edgeTolerance || u + v > 1 + edgeTolerance)
    return std::nullopt;
  const double distance = dot(edge2, secondPerpendicular) / determinant;
  return distance >= 0 ? std::optional(distance) : std::nullopt;
}

bool clipAxis(double origin, double direction, double minimum, double maximum,
              double &entry, double &exit) {
  if (direction == 0)
    return origin >= minimum && origin <= maximum;
  double near = (minimum - origin) / direction;
  double far = (maximum - origin) / direction;
  if (near > far)
    std::swap(near, far);
  entry = std::max(entry, near);
  exit = std::min(exit, far);
  return entry <= exit;
}

bool validField(const HeightField &field) {
  if (field.cellsX <= 0 || field.cellsZ <= 0 || field.size.x <= 0 ||
      field.size.y <= 0 || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y))
    return false;
  const auto columns = std::size_t(field.cellsX) + 1;
  const auto rows = std::size_t(field.cellsZ) + 1;
  return columns <= std::numeric_limits<std::size_t>::max() / rows &&
         field.heights.size() == columns * rows;
}
int containingCell(const HeightField &field, double coordinate, bool xAxis) {
  const int count = xAxis ? field.cellsX : field.cellsZ;
  const double extent = xAxis ? field.size.x : field.size.y;
  int cell = static_cast<int>(std::clamp(
      std::floor(coordinate / extent * count), 0.0, double(count - 1)));
  const auto boundary = [&](int index) {
    return xAxis ? field.position(index, 0).x : field.position(0, index).y;
  };
  // Compare against the renderer's float vertices, not ideal grid spacing.
  while (cell > 0 && coordinate < boundary(cell))
    --cell;
  while (cell < count - 1 && coordinate >= boundary(cell + 1))
    ++cell;
  return cell;
}
} // namespace

std::optional<float> sampleTerrainHeight(const HeightField &field,
                                         Vec2 position) {
  if (!validField(field) || !std::isfinite(position.x) ||
      !std::isfinite(position.y))
    return std::nullopt;
  if (position.x < 0 || position.y < 0 || position.x > field.size.x ||
      position.y > field.size.y)
    return std::nullopt;
  const int x = containingCell(field, position.x, true);
  const int z = containingCell(field, position.y, false);
  const auto minimum = field.position(x, z);
  const auto maximum = field.position(x + 1, z + 1);
  const double u =
      (double(position.x) - minimum.x) / (double(maximum.x) - minimum.x);
  const double v =
      (double(position.y) - minimum.y) / (double(maximum.y) - minimum.y);
  const double a = field.height(x, z);
  const double b = field.height(x + 1, z);
  const double c = field.height(x, z + 1);
  const double d = field.height(x + 1, z + 1);
  return static_cast<float>(u + v <= 1
                                ? a + (b - a) * u + (c - a) * v
                                : d + (c - d) * (1 - u) + (b - d) * (1 - v));
}

std::optional<Vec3> raycastTerrain(const HeightField &field, Vec3 origin,
                                   Vec3 direction) {
  if (!validField(field))
    return std::nullopt;
  for (float value :
       {origin.x, origin.y, origin.z, direction.x, direction.y, direction.z}) {
    if (!std::isfinite(value))
      return std::nullopt;
  }
  if (direction.x == 0 && direction.y == 0 && direction.z == 0)
    return std::nullopt;
  constexpr double minimumX = 0;
  constexpr double minimumZ = 0;
  double entry = 0;
  double exit = std::numeric_limits<double>::infinity();
  if (!clipAxis(origin.x, direction.x, minimumX, field.size.x, entry, exit) ||
      !clipAxis(origin.z, direction.z, minimumZ, field.size.y, entry, exit))
    return std::nullopt;

  const auto firstCell = [&](double coordinate, double direction, bool xAxis) {
    int cell = containingCell(field, coordinate, xAxis);
    const double boundary =
        xAxis ? field.position(cell, 0).x : field.position(0, cell).y;
    if (direction < 0 && coordinate == boundary && cell > 0)
      --cell;
    return cell;
  };
  int x = firstCell(origin.x + direction.x * entry, direction.x, true);
  int z = firstCell(origin.z + direction.z * entry, direction.z, false);
  const int stepX = direction.x > 0 ? 1 : -1;
  const int stepZ = direction.z > 0 ? 1 : -1;
  const Vector rayOrigin{origin.x, origin.y, origin.z};
  const Vector rayDirection{direction.x, direction.y, direction.z};
  const auto vertex = [&](int column, int row) {
    const auto position = field.position(column, row);
    return Vector{position.x, field.height(column, row), position.y};
  };

  while (x >= 0 && z >= 0 && x < field.cellsX && z < field.cellsZ) {
    const double nextX =
        direction.x == 0
            ? std::numeric_limits<double>::infinity()
            : (double(field.position(x + (stepX > 0 ? 1 : 0), 0).x) -
               origin.x) /
                  direction.x;
    const double nextZ =
        direction.z == 0
            ? std::numeric_limits<double>::infinity()
            : (double(field.position(0, z + (stepZ > 0 ? 1 : 0)).y) -
               origin.z) /
                  direction.z;
    const double cellExit = std::min({nextX, nextZ, exit});
    const auto a = vertex(x, z);
    const auto b = vertex(x + 1, z);
    const auto c = vertex(x, z + 1);
    const auto d = vertex(x + 1, z + 1);
    auto nearest = triangleHit(rayOrigin, rayDirection, a, c, b);
    if (auto second = triangleHit(rayOrigin, rayDirection, b, c, d);
        second && (!nearest || *second < *nearest))
      nearest = second;
    if (nearest && *nearest + 1e-5 >= entry && *nearest <= cellExit + 1e-5) {
      return Vec3{static_cast<float>(origin.x + direction.x * *nearest),
                  static_cast<float>(origin.y + direction.y * *nearest),
                  static_cast<float>(origin.z + direction.z * *nearest)};
    }
    if (cellExit >= exit || !std::isfinite(cellExit))
      break;
    entry = cellExit;
    if (nextX <= nextZ)
      x += stepX;
    if (nextZ <= nextX)
      z += stepZ;
  }
  return std::nullopt;
}
} // namespace demi::runtime
