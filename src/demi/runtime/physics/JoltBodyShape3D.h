#pragma once

#include "demi/runtime/physics/PhysicsBodyInputs3D.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace demi::runtime {

// Backend-only shape construction from a borrowed native input snapshot.
[[nodiscard]] JPH::ShapeRefC
createJoltBodyShape3D(const World &world, const PhysicsBodyInputs3D &inputs,
                      const WorldTransform3D &transform);
[[nodiscard]] JPH::ShapeRefC createJoltCapsuleShape3D(float radius,
                                                      float totalHeight);

} // namespace demi::runtime
