#pragma once

#include "demi/runtime/physics/ColliderAsset3D.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace demi::runtime {
// Backend-only factory. Hull coordinates share the asset's origin; leaf user
// data is a one-based index into the body's immutable part-ID snapshot.
[[nodiscard]] JPH::ShapeRefC
createJoltCompoundShape3D(const std::vector<ColliderPart3D> &parts, Vec3 scale);
} // namespace demi::runtime
