#include "demi/runtime/physics/SpatialQuery3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace demi::runtime {
namespace {

struct Aabb3D {
  Vec3 min;
  Vec3 max;
};

struct Sphere3D {
  Vec3 center;
  float radius = 0.0F;
};

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 subtract(const Vec3 left, const Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

float lengthSquared(const Vec3 value) { return dot(value, value); }

Vec3 normalize(const Vec3 value) {
  const float length = std::sqrt(lengthSquared(value));
  return length > 0.00001F
             ? Vec3{value.x / length, value.y / length, value.z / length}
             : Vec3{};
}

std::optional<WorldTransform3D>
entityTransform(const World &world, const Entity &entity,
                const Transform3DComponent *overrideTransform) {
  return overrideTransform != nullptr
             ? resolveWorldTransform3D(world, entity, *overrideTransform)
             : resolveWorldTransform3D(world, entity);
}

std::optional<Aabb3D>
boxBounds(const World &world, const Entity &entity,
          const Transform3DComponent *overrideTransform = nullptr) {
  const auto collider = resolvedBoxCollider3D(world, entity);
  const auto transform = entityTransform(world, entity, overrideTransform);
  if (!collider || !transform)
    return std::nullopt;

  const Vec3 half{collider->size.x * 0.5F, collider->size.y * 0.5F,
                  collider->size.z * 0.5F};
  Aabb3D bounds{.min = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()},
                .max = {std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()}};
  for (const float x : {-half.x, half.x})
    for (const float y : {-half.y, half.y})
      for (const float z : {-half.z, half.z}) {
        const Vec3 point = transformPoint3D(
            *transform, {collider->offset.x + x, collider->offset.y + y,
                         collider->offset.z + z});
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
      }
  return bounds;
}

std::optional<Sphere3D>
sphereBounds(const World &world, const Entity &entity,
             const Transform3DComponent *overrideTransform = nullptr) {
  const auto *collider = entity.component<SphereCollider3DComponent>();
  const auto transform = entityTransform(world, entity, overrideTransform);
  if (collider == nullptr || !transform)
    return std::nullopt;
  const float scale =
      std::max({std::abs(transform->scale.x), std::abs(transform->scale.y),
                std::abs(transform->scale.z)});
  return Sphere3D{.center = transformPoint3D(*transform, collider->offset),
                  .radius = collider->radius * scale};
}

bool intersects(const Aabb3D &left, const Aabb3D &right) {
  return left.min.x < right.max.x && left.max.x > right.min.x &&
         left.min.y < right.max.y && left.max.y > right.min.y &&
         left.min.z < right.max.z && left.max.z > right.min.z;
}

bool intersects(const Sphere3D &left, const Sphere3D &right) {
  const float radius = left.radius + right.radius;
  return lengthSquared(subtract(left.center, right.center)) < radius * radius;
}

bool intersects(const Sphere3D &sphere, const Aabb3D &box) {
  const Vec3 closest{std::clamp(sphere.center.x, box.min.x, box.max.x),
                     std::clamp(sphere.center.y, box.min.y, box.max.y),
                     std::clamp(sphere.center.z, box.min.z, box.max.z)};
  return lengthSquared(subtract(sphere.center, closest)) <
         sphere.radius * sphere.radius;
}

std::optional<float> raySphere(Vec3 origin, Vec3 direction,
                               const Sphere3D &sphere) {
  const Vec3 offset = subtract(origin, sphere.center);
  const float b = dot(offset, direction);
  const float c = dot(offset, offset) - sphere.radius * sphere.radius;
  const float discriminant = b * b - c;
  if (discriminant < 0.0F)
    return std::nullopt;
  const float root = std::sqrt(discriminant);
  const float near = -b - root;
  const float far = -b + root;
  if (near >= 0.0F)
    return near;
  return far >= 0.0F ? std::make_optional(far) : std::nullopt;
}

std::optional<float> rayBox(Vec3 origin, Vec3 direction, const Aabb3D &box) {
  float near = 0.0F;
  float far = std::numeric_limits<float>::max();
  const std::array origins{origin.x, origin.y, origin.z};
  const std::array directions{direction.x, direction.y, direction.z};
  const std::array minimums{box.min.x, box.min.y, box.min.z};
  const std::array maximums{box.max.x, box.max.y, box.max.z};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (std::abs(directions[axis]) < 0.00001F) {
      if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
        return std::nullopt;
      continue;
    }
    float first = (minimums[axis] - origins[axis]) / directions[axis];
    float second = (maximums[axis] - origins[axis]) / directions[axis];
    if (first > second)
      std::swap(first, second);
    near = std::max(near, first);
    far = std::min(far, second);
    if (near > far)
      return std::nullopt;
  }
  return near;
}

Vec3 boxNormal(const Vec3 point, const Aabb3D &box) {
  const std::array distances{
      std::abs(point.x - box.min.x), std::abs(point.x - box.max.x),
      std::abs(point.y - box.min.y), std::abs(point.y - box.max.y),
      std::abs(point.z - box.min.z), std::abs(point.z - box.max.z)};
  const auto closest = std::min_element(distances.begin(), distances.end());
  switch (std::distance(distances.begin(), closest)) {
  case 0:
    return {-1.0F, 0.0F, 0.0F};
  case 1:
    return {1.0F, 0.0F, 0.0F};
  case 2:
    return {0.0F, -1.0F, 0.0F};
  case 3:
    return {0.0F, 1.0F, 0.0F};
  case 4:
    return {0.0F, 0.0F, -1.0F};
  default:
    return {0.0F, 0.0F, 1.0F};
  }
}

} // namespace

bool collidersOverlap3D(const World &world, const Entity &left,
                        const Transform3DComponent *leftLocalOverride,
                        const Entity &right) {
  const auto leftBox = boxBounds(world, left, leftLocalOverride);
  const auto rightBox = boxBounds(world, right);
  if (leftBox && rightBox)
    return intersects(*leftBox, *rightBox);
  const auto leftSphere = sphereBounds(world, left, leftLocalOverride);
  const auto rightSphere = sphereBounds(world, right);
  if (leftSphere && rightSphere)
    return intersects(*leftSphere, *rightSphere);
  if (leftSphere && rightBox)
    return intersects(*leftSphere, *rightBox);
  return leftBox && rightSphere && intersects(*rightSphere, *leftBox);
}

std::vector<std::string> overlapSphere3D(const World &world, const Vec3 center,
                                         const float radius,
                                         const std::string &ignoredEntityId) {
  std::vector<std::string> entities;
  const Sphere3D query{.center = center, .radius = std::max(radius, 0.0F)};
  for (const Entity &entity : world.entities) {
    if (entity.id == ignoredEntityId)
      continue;
    const auto sphere = sphereBounds(world, entity);
    const auto box = boxBounds(world, entity);
    if ((sphere && intersects(query, *sphere)) ||
        (box && intersects(query, *box)))
      entities.push_back(entity.id);
  }
  std::ranges::sort(entities);
  return entities;
}

std::optional<PhysicsRaycastHit3D>
raycast3D(const World &world, const Vec3 origin, const Vec3 direction,
          const float distance, const std::string &ignoredEntityId) {
  const Vec3 rayDirection = normalize(direction);
  if (lengthSquared(rayDirection) <= 0.0F || distance < 0.0F)
    return std::nullopt;
  std::optional<PhysicsRaycastHit3D> nearest;
  for (const Entity &entity : world.entities) {
    if (entity.id == ignoredEntityId)
      continue;
    std::optional<float> hitDistance;
    Vec3 normal;
    if (const auto sphere = sphereBounds(world, entity)) {
      hitDistance = raySphere(origin, rayDirection, *sphere);
      if (hitDistance) {
        const Vec3 point{origin.x + rayDirection.x * *hitDistance,
                         origin.y + rayDirection.y * *hitDistance,
                         origin.z + rayDirection.z * *hitDistance};
        normal = normalize(subtract(point, sphere->center));
      }
    } else if (const auto box = boxBounds(world, entity)) {
      hitDistance = rayBox(origin, rayDirection, *box);
      if (hitDistance) {
        const Vec3 point{origin.x + rayDirection.x * *hitDistance,
                         origin.y + rayDirection.y * *hitDistance,
                         origin.z + rayDirection.z * *hitDistance};
        normal = boxNormal(point, *box);
      }
    }
    if (!hitDistance || *hitDistance > distance ||
        (nearest && nearest->distance <= *hitDistance))
      continue;
    nearest =
        PhysicsRaycastHit3D{.entityId = entity.id,
                            .point = {origin.x + rayDirection.x * *hitDistance,
                                      origin.y + rayDirection.y * *hitDistance,
                                      origin.z + rayDirection.z * *hitDistance},
                            .normal = normal,
                            .distance = *hitDistance};
  }
  return nearest;
}

} // namespace demi::runtime
