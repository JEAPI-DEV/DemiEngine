#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <array>

namespace demi::runtime {

bool hasSolidCollider3D(const Entity &entity) {
  return (entity.hasComponent<BoxCollider3DComponent>() &&
          !entity.component<BoxCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<SphereCollider3DComponent>() &&
          !entity.component<SphereCollider3DComponent>()->isTrigger);
}

bool collidesAt3D(const World &world, const Entity &moving,
                  const Vec3 position) {
  if (!hasSolidCollider3D(moving) ||
      !moving.hasComponent<Transform3DComponent>()) {
    return false;
  }
  Transform3DComponent candidate = *moving.component<Transform3DComponent>();
  candidate.position = position;
  for (const Entity &other : world.entities) {
    if (other.id == moving.id || !other.hasComponent<Transform3DComponent>() ||
        !hasSolidCollider3D(other)) {
      continue;
    }
    if (other.hasComponent<Rigidbody3DComponent>() &&
        other.component<Rigidbody3DComponent>()->bodyType != "static") {
      continue;
    }
    if (collidersOverlap3D(world, moving, &candidate, other)) {
      return true;
    }
  }
  return false;
}

Vec3 resolveDynamicMove3D(const World &world, const Entity &entity,
                          const Vec3 from, const Vec3 delta) {
  if (!entity.hasComponent<Rigidbody3DComponent>() ||
      entity.component<Rigidbody3DComponent>()->bodyType != "dynamic" ||
      !hasSolidCollider3D(entity)) {
    return Vec3{
        .x = from.x + delta.x, .y = from.y + delta.y, .z = from.z + delta.z};
  }

  Vec3 resolved = from;
  const std::array<Vec3, 3> axes{{
      Vec3{.x = delta.x, .y = 0.0F, .z = 0.0F},
      Vec3{.x = 0.0F, .y = delta.y, .z = 0.0F},
      Vec3{.x = 0.0F, .y = 0.0F, .z = delta.z},
  }};
  for (const Vec3 axis : axes) {
    const Vec3 candidate{.x = resolved.x + axis.x,
                         .y = resolved.y + axis.y,
                         .z = resolved.z + axis.z};
    if (!collidesAt3D(world, entity, candidate)) {
      resolved = candidate;
    }
  }
  return resolved;
}

} // namespace demi::runtime
