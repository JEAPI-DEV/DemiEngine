#pragma once

#include "demi/runtime/SceneData.h"

namespace demi::runtime {

[[nodiscard]] bool hasSolidCollider3D(const Entity& entity);
[[nodiscard]] bool collidesAt3D(const World& world, const Entity& moving, Vec3 position);
[[nodiscard]] Vec3 resolveDynamicMove3D(const World& world, const Entity& entity, Vec3 from, Vec3 delta);

} // namespace demi::runtime
