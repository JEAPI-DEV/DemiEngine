#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <array>

namespace demi::runtime {

namespace {

struct Aabb3D {
  Vec3 min;
  Vec3 max;
};

[[nodiscard]] Aabb3D boxColliderAabb3D(const Entity &entity,
                                       const Vec3 position) {
  const BoxCollider3DComponent &collider =
      *entity.component<BoxCollider3DComponent>();
  const Vec3 center{
      .x = position.x + collider.offset.x,
      .y = position.y + collider.offset.y,
      .z = position.z + collider.offset.z,
  };
  const Vec3 half{.x = collider.size.x * 0.5F,
                  .y = collider.size.y * 0.5F,
                  .z = collider.size.z * 0.5F};
  return Aabb3D{
      .min = Vec3{.x = center.x - half.x,
                  .y = center.y - half.y,
                  .z = center.z - half.z},
      .max = Vec3{.x = center.x + half.x,
                  .y = center.y + half.y,
                  .z = center.z + half.z},
  };
}

[[nodiscard]] Vec3 sphereCenter3D(const Entity &entity, const Vec3 position) {
  const SphereCollider3DComponent &collider =
      *entity.component<SphereCollider3DComponent>();
  return Vec3{.x = position.x + collider.offset.x,
              .y = position.y + collider.offset.y,
              .z = position.z + collider.offset.z};
}

[[nodiscard]] bool intersects(const Aabb3D &left, const Aabb3D &right) {
  return left.min.x < right.max.x && left.max.x > right.min.x &&
         left.min.y < right.max.y && left.max.y > right.min.y &&
         left.min.z < right.max.z && left.max.z > right.min.z;
}

[[nodiscard]] float distanceSquared(const Vec3 left, const Vec3 right) {
  const float dx = left.x - right.x;
  const float dy = left.y - right.y;
  const float dz = left.z - right.z;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] bool sphereIntersectsAabb(const Vec3 center, const float radius,
                                        const Aabb3D &aabb) {
  const float closestX = std::clamp(center.x, aabb.min.x, aabb.max.x);
  const float closestY = std::clamp(center.y, aabb.min.y, aabb.max.y);
  const float closestZ = std::clamp(center.z, aabb.min.z, aabb.max.z);
  return distanceSquared(center,
                         Vec3{.x = closestX, .y = closestY, .z = closestZ}) <
         radius * radius;
}

[[nodiscard]] bool collidersIntersect3D(const Entity &left,
                                        const Vec3 leftPosition,
                                        const Entity &right,
                                        const Vec3 rightPosition) {
  if (left.hasComponent<BoxCollider3DComponent>() &&
      right.hasComponent<BoxCollider3DComponent>()) {
    return intersects(boxColliderAabb3D(left, leftPosition),
                      boxColliderAabb3D(right, rightPosition));
  }
  if (left.hasComponent<SphereCollider3DComponent>() &&
      right.hasComponent<SphereCollider3DComponent>()) {
    const float radius = left.component<SphereCollider3DComponent>()->radius +
                         right.component<SphereCollider3DComponent>()->radius;
    return distanceSquared(sphereCenter3D(left, leftPosition),
                           sphereCenter3D(right, rightPosition)) <
           radius * radius;
  }
  if (left.hasComponent<SphereCollider3DComponent>() &&
      right.hasComponent<BoxCollider3DComponent>()) {
    return sphereIntersectsAabb(
        sphereCenter3D(left, leftPosition),
        left.component<SphereCollider3DComponent>()->radius,
        boxColliderAabb3D(right, rightPosition));
  }
  if (left.hasComponent<BoxCollider3DComponent>() &&
      right.hasComponent<SphereCollider3DComponent>()) {
    return sphereIntersectsAabb(
        sphereCenter3D(right, rightPosition),
        right.component<SphereCollider3DComponent>()->radius,
        boxColliderAabb3D(left, leftPosition));
  }
  return false;
}

} // namespace

bool hasSolidCollider3D(const Entity &entity) {
  return (entity.hasComponent<BoxCollider3DComponent>() &&
          !entity.component<BoxCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<SphereCollider3DComponent>() &&
          !entity.component<SphereCollider3DComponent>()->isTrigger);
}

bool collidesAt3D(const World &world, const Entity &moving,
                  const Vec3 position) {
  if (!hasSolidCollider3D(moving)) {
    return false;
  }
  for (const Entity &other : world.entities) {
    if (other.id == moving.id || !other.hasComponent<Transform3DComponent>() ||
        !hasSolidCollider3D(other)) {
      continue;
    }
    if (other.hasComponent<Rigidbody3DComponent>() &&
        other.component<Rigidbody3DComponent>()->bodyType != "static") {
      continue;
    }
    if (collidersIntersect3D(
            moving, position, other,
            other.component<Transform3DComponent>()->position)) {
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
