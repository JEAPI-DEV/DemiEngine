#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct DestructionImpact3D {
  Vec3 position;
  float radius = 0.5F;
  float energy = 0;   // Joules allocated to fracture, not an implicit impulse.
  float impulse = 0;  // Total momentum budget, in N*s.
  Vec3 direction;     // Zero selects radial impulse directions.
  std::string entity; // Optional assembly/root-fragment filter.
};
bool validateDestructionImpact3D(const DestructionImpact3D &,
                                 std::string &error);
float destructionImpactWeight(float distance, float radius);
Vec3 destructionImpactDirection(const DestructionImpact3D &, Vec3 contact);
} // namespace demi::runtime
