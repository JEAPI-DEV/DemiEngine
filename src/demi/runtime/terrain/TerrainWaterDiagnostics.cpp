#include "demi/runtime/terrain/TerrainWaterDiagnostics.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
namespace {
bool isRiverSide(const TerrainWaterBodySpec &body, Vec2 position,
                 Vec2 neighbour) {
  double nearestDistance = std::numeric_limits<double>::infinity();
  double directionX = 0;
  double directionZ = 0;
  bool beyondEndpoint = true;
  for (std::size_t segment = 0; segment + 1 < body.riverPath.size();
       ++segment) {
    const auto &start = body.riverPath[segment];
    const auto &end = body.riverPath[segment + 1];
    const double dx = double(end.x) - start.x;
    const double dz = double(end.z) - start.z;
    const double lengthSquared = dx * dx + dz * dz;
    if (!(lengthSquared > 0)) {
      continue;
    }
    const double projection = ((double(position.x) - start.x) * dx +
                               (double(position.y) - start.z) * dz) /
                              lengthSquared;
    const double along = std::clamp(projection, 0.0, 1.0);
    const double distance =
        std::hypot(double(position.x) - start.x - along * dx,
                   double(position.y) - start.z - along * dz);
    if (distance >= nearestDistance) {
      continue;
    }
    nearestDistance = distance;
    directionX = dx;
    directionZ = dz;
    beyondEndpoint = (segment == 0 && projection <= 0) ||
                     (segment + 2 == body.riverPath.size() && projection >= 1);
  }
  if (beyondEndpoint) {
    return false;
  }
  const double offsetX = double(neighbour.x) - position.x;
  const double offsetZ = double(neighbour.y) - position.y;
  const double across = std::abs(offsetX * directionZ - offsetZ * directionX);
  const double along = std::abs(offsetX * directionX + offsetZ * directionZ);
  // Inlets and outlets may remain open. Diagnose lateral edges rather than
  // treating every finite channel as a closed lake.
  return across > 0 && across >= along;
}
} // namespace
std::vector<std::string>
terrainWaterContainmentWarnings(const HeightField &ground,
                                const TerrainWaterAuthoring &authoring) {
  std::vector<std::string> warnings;
  if (ground.cellsX <= 0 || ground.cellsZ <= 0 ||
      !std::isfinite(ground.size.x) || !std::isfinite(ground.size.y) ||
      ground.size.x <= 0 || ground.size.y <= 0)
    throw std::invalid_argument(
        "Water containment requires a valid terrain grid");
  const auto coverage = terrain_water_detail::buildWaterLevelField(
      ground.size, ground.cellsX, ground.cellsZ, ground.heights, authoring);
  if (coverage.samples() != ground.heights.size())
    throw std::invalid_argument(
        "Water containment sample count does not match the terrain grid");
  std::vector<bool> spills(authoring.bodies.size());
  constexpr std::array<std::array<int, 2>, 4> neighbours{
      {{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}}};
  for (int z = 0; z <= ground.cellsZ; ++z) {
    for (int x = 0; x <= ground.cellsX; ++x) {
      const auto sample = coverage.sampleAt(x, z);
      if (!coverage.wet[sample])
        continue;
      const auto bodyIndex = coverage.body[sample];
      const auto &body = authoring.bodies.at(bodyIndex);
      const bool finiteLake = body.kind == TerrainWaterBody::Lake &&
                              (body.radius > 0 || !body.boundary.empty());
      const bool river = body.kind == TerrainWaterBody::River;
      if ((!finiteLake && !river) || spills[bodyIndex]) {
        continue;
      }
      for (const auto &offset : neighbours) {
        const int nextX = x + offset[0], nextZ = z + offset[1];
        if (nextX < 0 || nextZ < 0 || nextX > ground.cellsX ||
            nextZ > ground.cellsZ) {
          if (river) {
            continue;
          }
          spills[bodyIndex] = ground.heights[sample] < body.level;
          if (spills[bodyIndex])
            break;
          continue;
        }
        const auto next = coverage.sampleAt(nextX, nextZ);
        // Interior samples and joins cannot expose a dry bank. Avoid walking
        // the river path for their neighbours.
        if (coverage.wet[next]) {
          continue;
        }
        if (river && !isRiverSide(body, coverage.position(x, z),
                                  coverage.position(nextX, nextZ))) {
          continue;
        }
        const float localLevel = coverage.bodyLevels[bodyIndex].at(next);
        if (ground.heights[next] < localLevel) {
          spills[bodyIndex] = true;
          break;
        }
      }
    }
  }
  for (std::size_t body = 0; body < spills.size(); ++body) {
    if (!spills[body]) {
      continue;
    }
    const auto &spec = authoring.bodies[body];
    if (spec.kind == TerrainWaterBody::River) {
      warnings.push_back(
          "River '" + spec.id +
          "' reaches a side boundary below its local water surface. It is not "
          "contained by terrain banks; carving cuts ground and does not raise "
          "banks. Adjust its bed, depth or path, or sculpt banks. No flood "
          "simulation is performed.");
    } else {
      warnings.push_back(
          "Lake '" + spec.id +
          "' reaches its authored boundary below the water level. It is not "
          "contained by terrain banks; lower its level, enlarge its boundary, "
          "or sculpt banks. No flood simulation is performed.");
    }
  }
  return warnings;
}
} // namespace demi::runtime
