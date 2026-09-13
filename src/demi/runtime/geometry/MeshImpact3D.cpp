#include "demi/runtime/geometry/MeshImpact3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime {
float normalImpactEnergy3D(Vec3 relativeVelocity, Vec3 normal,
                           float inverseMassSum) {
  if (inverseMassSum <= 0 || !std::isfinite(inverseMassSum))
    return 0;
  const double speed = double(relativeVelocity.x) * normal.x +
                       double(relativeVelocity.y) * normal.y +
                       double(relativeVelocity.z) * normal.z;
  if (!std::isfinite(speed) || speed <= 0)
    return 0;
  return static_cast<float>(
      std::min(0.5 * speed * speed / inverseMassSum,
               double(std::numeric_limits<float>::max())));
}

MeshImpactResult3D meshImpactDepth3D(float energy,
                                     const MeshImpactMaterial3D &m) {
  if (!std::isfinite(energy) || energy < 0 || !std::isfinite(m.radius) ||
      m.radius <= 0 || !std::isfinite(m.yieldEnergy) || m.yieldEnergy < 0 ||
      !std::isfinite(m.stiffness) || m.stiffness <= 0 ||
      !std::isfinite(m.absorption) || m.absorption < 0 || m.absorption > 1 ||
      !std::isfinite(m.maximumDepth) || m.maximumDepth <= 0) {
    return {
        .error =
            "Impact energy/material values must be finite and nonnegative; "
            "radius, stiffness and max_depth positive; absorption in [0,1]."};
  }
  const double available =
      std::max(double(energy) * m.absorption - m.yieldEnergy, 0.0);
  const double depth = std::sqrt(2.0 * available / m.stiffness);
  return {.accepted = true,
          .depth = static_cast<float>(std::min(
              depth, double(std::min(m.maximumDepth, m.radius * 0.5F))))};
}

MeshImpactResult3D impactMesh3D(World &world, const std::string &id, Vec3 point,
                                Vec3 forceDirection, float energy,
             const MeshImpactMaterial3D &material) {
  const auto *entity = findEntity(world, id);
  if (!entity || !entity->hasComponent<Dentable3DComponent>())
    return {.error = "Mesh denting requires the Dentable3D component."};
  auto result = meshImpactDepth3D(energy, material);
  if (result.accepted && result.depth > 0)
    result.accepted = dentMesh3D(world, id, point, forceDirection,
                                 material.radius, result.depth, result.error);
  return result;
}
} // namespace demi::runtime
