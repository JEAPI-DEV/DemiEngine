#include "demi/runtime/terrain/TerrainWaterFlow.h"

#include "demi/runtime/terrain/TerrainWater.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
void validateTerrainWaterFlow(const TerrainWaterBodySpec &body) {
  if (!std::isfinite(body.flowSpeed) || body.flowSpeed < 0.F) {
    throw std::invalid_argument(
        "Water flow speed must be finite and nonnegative");
  }
  if (!std::isfinite(body.flowDirection.x) ||
      !std::isfinite(body.flowDirection.y)) {
    throw std::invalid_argument("Water flow direction must be finite");
  }
  if (body.kind != TerrainWaterBody::River && body.flowSpeed > 0.F &&
      std::hypot(body.flowDirection.x, body.flowDirection.y) <= 0.F) {
    throw std::invalid_argument(
        "Moving lake/ocean water needs a nonzero flow direction");
  }
}

bool sameTerrainWaterFlow(const TerrainWaterBodySpec &before,
                          const TerrainWaterBodySpec &after) {
  const bool sameDirection = before.flowDirection.x == after.flowDirection.x &&
                             before.flowDirection.y == after.flowDirection.y;
  if (before.kind != after.kind || before.flowSpeed != after.flowSpeed ||
      !sameDirection) {
    return false;
  }
  if (before.kind != TerrainWaterBody::River) {
    return true;
  }
  return std::ranges::equal(
      before.riverPath, after.riverPath, [](Vec3 left, Vec3 right) {
        return left.x == right.x && left.y == right.y && left.z == right.z;
      });
}

void parseTerrainWaterFlow(const nlohmann::json &value,
                           TerrainWaterBodySpec &body) {
  body.flowSpeed = value.value("flow_speed", body.flowSpeed);
  if (value.contains("flow_direction")) {
    const auto &direction = value.at("flow_direction");
    if (!direction.is_array() || direction.size() != 2 ||
        !direction.at(0).is_number() || !direction.at(1).is_number()) {
      throw std::invalid_argument(
          "Water flow direction requires two XZ numbers");
    }
    body.flowDirection = {direction.at(0).get<float>(),
                          direction.at(1).get<float>()};
  }
  validateTerrainWaterFlow(body);
}

Vec3 terrainWaterFlowVelocity(const TerrainWaterBodySpec &body,
                              Vec3 localPosition, Vec3 surfaceNormal) {
  validateTerrainWaterFlow(body);
  if (body.flowSpeed == 0.F) {
    return {};
  }
  double directionX = body.flowDirection.x;
  double directionZ = body.flowDirection.y;
  if (body.kind == TerrainWaterBody::River) {
    double nearestDistance = std::numeric_limits<double>::infinity();
    directionX = 0;
    directionZ = 0;
    for (std::size_t index = 1; index < body.riverPath.size(); ++index) {
      const auto &start = body.riverPath[index - 1];
      const auto &end = body.riverPath[index];
      const double dx = double(end.x) - start.x;
      const double dz = double(end.z) - start.z;
      const double lengthSquared = dx * dx + dz * dz;
      if (!(lengthSquared > 0)) {
        continue;
      }
      const double offsetX = double(localPosition.x) - start.x;
      const double offsetZ = double(localPosition.z) - start.z;
      const double along =
          std::clamp((offsetX * dx + offsetZ * dz) / lengthSquared, 0.0, 1.0);
      const double distanceX = offsetX - along * dx;
      const double distanceZ = offsetZ - along * dz;
      const double distance = distanceX * distanceX + distanceZ * distanceZ;
      if (distance < nearestDistance) {
        nearestDistance = distance;
        directionX = dx;
        directionZ = dz;
      }
    }
  }
  const double length = std::hypot(directionX, directionZ);
  if (!(length > 0)) {
    return {};
  }
  double directionY = 0;
  if (body.riverSurfaceMode == TerrainRiverSurfaceMode::FollowBed &&
      surfaceNormal.y > 0.F) {
    // Preserve the authored downstream XZ heading while following the actual
    // rendered triangle, then restore the prescribed local speed in 3D.
    directionY =
        -(directionX * surfaceNormal.x + directionZ * surfaceNormal.z) /
        surfaceNormal.y;
  }
  const double tangentLength = std::hypot(directionX, directionY, directionZ);
  return {float(directionX / tangentLength * body.flowSpeed),
          float(directionY / tangentLength * body.flowSpeed),
          float(directionZ / tangentLength * body.flowSpeed)};
}
} // namespace demi::runtime
