#pragma once
#include "demi/runtime/geometry/MeshDeformation3D.h"

namespace demi::runtime {
struct MeshImpactMaterial3D {
  float radius = 0.32F;
  float yieldEnergy = 4.0F;  // joules before permanent damage
  float stiffness = 4000.0F; // effective force/displacement, N/m
  float absorption = 0.7F;
  float maximumDepth = 0.15F;
};
struct MeshImpactResult3D {
  bool accepted = false;
  float depth = 0;
  std::string error;
};
// Normal closing energy for two bodies, using their reduced translational mass.
[[nodiscard]] float normalImpactEnergy3D(Vec3 relativeVelocity, Vec3 normal,
                                         float inverseMassSum);
[[nodiscard]] MeshImpactResult3D
meshImpactDepth3D(float energy, const MeshImpactMaterial3D &material);
[[nodiscard]] MeshImpactResult3D
impactMesh3D(World &world, const std::string &id, Vec3 point,
             Vec3 forceDirection, float energy,
             const MeshImpactMaterial3D &material);
} // namespace demi::runtime
