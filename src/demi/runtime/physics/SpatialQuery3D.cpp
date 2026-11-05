#include "demi/runtime/physics/SpatialQuery3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/JoltBodyShape3D.h"
#include "demi/runtime/physics/JoltLifetime.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>

#include <algorithm>
#include <cmath>

namespace demi::runtime {
namespace {

void ensureQueryRuntime() {
  // Cold editor queries have no simulation world to retain Jolt registration.
  // Keep only the shared registration lease; no bodies or query world are made.
  static const JoltLifetime lifetime;
}

JPH::Vec3 jolt(const Vec3 value) { return {value.x, value.y, value.z}; }

Vec3 demi(const JPH::Vec3 value) {
  return {value.GetX(), value.GetY(), value.GetZ()};
}

bool finite(const Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool validCast(const Vec3 origin, const Vec3 direction, const float distance) {
  return finite(origin) && finite(direction) && std::isfinite(distance) &&
         distance >= 0.0F;
}

std::optional<JPH::TransformedShape>
queryShape(const World &world, const Entity &entity,
           const Transform3DComponent *localOverride = nullptr) {
  const auto transform =
      localOverride != nullptr
          ? resolveWorldTransform3D(world, entity, *localOverride)
          : resolveWorldTransform3D(world, entity);
  if (!transform) {
    return std::nullopt;
  }
  const auto shape =
      createJoltBodyShape3D(world, physicsBodyInputs3D(entity), *transform);
  if (!shape) {
    return std::nullopt;
  }
  JPH::TransformedShape result(JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                               shape.GetPtr(), JPH::BodyID());
  result.SetWorldTransform(
      JPH::RVec3(transform->position.x, transform->position.y,
                 transform->position.z),
      JPH::Quat::sEulerAngles(jolt(transform->rotation)), JPH::Vec3::sOne());
  return result;
}

bool queryIncludes(const PhysicsBodyInputs3D &inputs, const std::string &layer,
                   const std::string &ignoredEntityId,
                   const bool includeTriggers = true) {
  return inputs.entity.enabled && inputs.entity.id != ignoredEntityId &&
         inputs.transform != nullptr && inputs.hasCollider() &&
         (inputs.body == nullptr || inputs.body->bodyEnabled) &&
         (layer.empty() || inputs.layer == layer) &&
         (includeTriggers || !inputs.isTrigger);
}

PhysicsQueryHit3D queryHit(const PhysicsBodyInputs3D &inputs,
                           const JPH::Vec3 point, const JPH::Vec3 normal,
                           const float distance = 0.0F,
                           const float fraction = 0.0F) {
  return {.entityId = inputs.entity.id,
          .layer = std::string(inputs.layer),
          .point = demi(point),
          .normal = demi(normal),
          .distance = distance,
          .fraction = fraction,
          .isTrigger = inputs.isTrigger};
}

const PhysicsWorld3D *synchronizedPhysics(const World &world) {
  const auto *physics = world.physicsWorld3D.get();
  return physics != nullptr && physics->hasSynchronizedBodies(world) ? physics
                                                                     : nullptr;
}

std::vector<PhysicsQueryHit3D>
overlapShape(const World &world, const JPH::Shape &shape, const Vec3 center,
             const std::string &layer, const std::string &ignoredEntityId) {
  std::vector<PhysicsQueryHit3D> result;
  const auto transform =
      JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
  for (const Entity &entity : world.entities) {
    const auto inputs = physicsBodyInputs3D(entity);
    if (!queryIncludes(inputs, layer, ignoredEntityId)) {
      continue;
    }
    const auto target = queryShape(world, entity);
    if (!target) {
      continue;
    }
    JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
    target->CollideShape(&shape, JPH::Vec3::sOne(), transform, {},
                         JPH::RVec3::sZero(), collector);
    if (collector.HadHit()) {
      const auto &hit = collector.mHit;
      result.push_back(
          queryHit(inputs, hit.mContactPointOn2,
                   -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY())));
    }
  }
  std::ranges::sort(result, {}, &PhysicsQueryHit3D::entityId);
  return result;
}

std::optional<PhysicsQueryHit3D>
castShape(const World &world, const JPH::Shape &shape, const Vec3 origin,
          const Vec3 direction, const float distance, const std::string &layer,
          const std::string &ignoredEntityId,
          const bool includeTriggers = true) {
  const auto unit = jolt(direction).NormalizedOr(JPH::Vec3::sZero());
  if (distance < 0.0F || unit.IsNearZero()) {
    return std::nullopt;
  }
  const auto cast = JPH::RShapeCast::sFromWorldTransform(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
      unit * distance);
  std::optional<PhysicsQueryHit3D> nearest;
  for (const Entity &entity : world.entities) {
    const auto inputs = physicsBodyInputs3D(entity);
    if (!queryIncludes(inputs, layer, ignoredEntityId, includeTriggers)) {
      continue;
    }
    const auto target = queryShape(world, entity);
    if (!target) {
      continue;
    }
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    target->CastShape(cast, {}, JPH::RVec3::sZero(), collector);
    if (!collector.HadHit()) {
      continue;
    }
    const auto &hit = collector.mHit;
    if (!nearest || hit.mFraction < nearest->fraction ||
        (hit.mFraction == nearest->fraction && entity.id < nearest->entityId)) {
      nearest =
          queryHit(inputs, hit.mContactPointOn2,
                   -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()),
                   hit.mFraction * distance, hit.mFraction);
    }
  }
  return nearest;
}

} // namespace

bool collidersOverlap3D(const World &world, const Entity &left,
                        const Transform3DComponent *leftLocalOverride,
                        const Entity &right) {
  ensureQueryRuntime();
  const auto leftShape = queryShape(world, left, leftLocalOverride);
  const auto rightShape = queryShape(world, right);
  if (!leftShape || !rightShape) {
    return false;
  }
  // Static-only shapes from our factory are triangle meshes. Jolt has no
  // mesh-vs-mesh dispatch; convex compounds remain eligible on either side.
  if (leftShape->mShape->MustBeStatic() && rightShape->mShape->MustBeStatic()) {
    return false;
  }
  JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
  JPH::CollideShapeSettings settings;
  // Authored triangle surfaces block scripted movement from either side.
  settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
  rightShape->CollideShape(leftShape->mShape.GetPtr(),
                           leftShape->GetShapeScale(),
                           leftShape->GetCenterOfMassTransform(), settings,
                           JPH::RVec3::sZero(), collector);
  return collector.HadHit() && collector.mHit.mPenetrationDepth > 0.0F;
}

std::vector<std::string> overlapSphere3D(const World &world, const Vec3 center,
                                         const float radius,
                                         const std::string &ignoredEntityId) {
  std::vector<std::string> entities;
  for (const PhysicsQueryHit3D &hit :
       overlapSphereAll3D(world, center, radius, {}, ignoredEntityId)) {
    entities.push_back(hit.entityId);
  }
  return entities;
}

std::vector<PhysicsQueryHit3D>
overlapSphereAll3D(const World &world, const Vec3 center, const float radius,
                   const std::string &layer,
                   const std::string &ignoredEntityId) {
  if (!finite(center) || !std::isfinite(radius) || radius < 0.0F) {
    return {};
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->overlapSphere(center, radius, layer, ignoredEntityId);
  }
  ensureQueryRuntime();
  const JPH::SphereShape shape(std::max(radius, 0.001F));
  return overlapShape(world, shape, center, layer, ignoredEntityId);
}

std::vector<PhysicsQueryHit3D>
overlapBoxAll3D(const World &world, const Vec3 center, const Vec3 size,
                const std::string &layer, const std::string &ignoredEntityId) {
  if (!finite(center) || !finite(size) || size.x < 0.0F || size.y < 0.0F ||
      size.z < 0.0F) {
    return {};
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->overlapBox(center, size, layer, ignoredEntityId);
  }
  ensureQueryRuntime();
  const JPH::BoxShape shape(JPH::Vec3(std::max(size.x * 0.5F, 0.001F),
                                      std::max(size.y * 0.5F, 0.001F),
                                      std::max(size.z * 0.5F, 0.001F)));
  return overlapShape(world, shape, center, layer, ignoredEntityId);
}

std::vector<PhysicsQueryHit3D>
overlapCapsuleAll3D(const World &world, const Vec3 center, const float radius,
                    const float height, const std::string &layer,
                    const std::string &ignoredEntityId) {
  if (!finite(center) || !std::isfinite(radius) || !std::isfinite(height) ||
      radius < 0.0F || height < 2.0F * radius) {
    return {};
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->overlapCapsule(center, radius, height, layer,
                                   ignoredEntityId);
  }
  ensureQueryRuntime();
  const auto shape = createJoltCapsuleShape3D(radius, height);
  return overlapShape(world, *shape, center, layer, ignoredEntityId);
}

std::optional<PhysicsRaycastHit3D>
raycast3D(const World &world, const Vec3 origin, const Vec3 direction,
          const float distance, const std::string &ignoredEntityId) {
  if (!validCast(origin, direction, distance)) {
    return std::nullopt;
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->raycast(origin, direction, distance, {}, ignoredEntityId);
  }
  ensureQueryRuntime();
  const auto unit = jolt(direction).NormalizedOr(JPH::Vec3::sZero());
  if (distance < 0.0F || unit.IsNearZero()) {
    return std::nullopt;
  }
  const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                          unit * distance);
  std::optional<PhysicsRaycastHit3D> nearest;
  for (const Entity &entity : world.entities) {
    const auto inputs = physicsBodyInputs3D(entity);
    if (!queryIncludes(inputs, {}, ignoredEntityId)) {
      continue;
    }
    const auto target = queryShape(world, entity);
    if (!target) {
      continue;
    }
    JPH::RayCastResult result;
    if (!target->CastRay(ray, result) ||
        (nearest && (result.mFraction > nearest->fraction ||
                     (result.mFraction == nearest->fraction &&
                      entity.id >= nearest->entityId)))) {
      continue;
    }
    const auto point = ray.GetPointOnRay(result.mFraction);
    nearest =
        queryHit(inputs, JPH::Vec3(point),
                 target->GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point),
                 result.mFraction * distance, result.mFraction);
    const auto part = target->GetSubShapeUserData(result.mSubShapeID2);
    if (const auto *parts = resolvedCompoundCollider3D(world, entity);
        parts != nullptr && part > 0 && part <= parts->size()) {
      nearest->colliderPartId = (*parts)[part - 1].id;
    }
  }
  return nearest;
}

std::optional<PhysicsQueryHit3D>
sphereCast3D(const World &world, const Vec3 origin, const float radius,
             const Vec3 direction, const float distance,
             const std::string &layer, const std::string &ignoredEntityId,
             const bool includeTriggers) {
  if (!validCast(origin, direction, distance) || !std::isfinite(radius) ||
      radius < 0.0F) {
    return std::nullopt;
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->castSphere(origin, radius, direction, distance, layer,
                               ignoredEntityId, includeTriggers);
  }
  ensureQueryRuntime();
  const JPH::SphereShape shape(std::max(radius, 0.001F));
  return castShape(world, shape, origin, direction, distance, layer,
                   ignoredEntityId, includeTriggers);
}

std::optional<PhysicsQueryHit3D>
capsuleCast3D(const World &world, const Vec3 origin, const float radius,
              const float height, const Vec3 direction, const float distance,
              const std::string &layer, const std::string &ignoredEntityId) {
  if (!validCast(origin, direction, distance) || !std::isfinite(radius) ||
      !std::isfinite(height) || radius < 0.0F || height < 2.0F * radius) {
    return std::nullopt;
  }

  if (const auto *physics = synchronizedPhysics(world)) {
    return physics->castCapsule(origin, radius, height, direction, distance,
                                layer, ignoredEntityId);
  }
  ensureQueryRuntime();
  const auto shape = createJoltCapsuleShape3D(radius, height);
  return castShape(world, *shape, origin, direction, distance, layer,
                   ignoredEntityId);
}

} // namespace demi::runtime
