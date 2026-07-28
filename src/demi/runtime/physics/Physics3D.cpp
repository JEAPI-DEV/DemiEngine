#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace demi::runtime {

bool hasSolidCollider3D(const Entity &entity) {
  return (entity.hasComponent<BoxCollider3DComponent>() &&
          !entity.component<BoxCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<SphereCollider3DComponent>() &&
          !entity.component<SphereCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<CapsuleCollider3DComponent>() &&
          !entity.component<CapsuleCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<ConvexCollider3DComponent>() &&
          !entity.component<ConvexCollider3DComponent>()->isTrigger) ||
         (entity.hasComponent<ModelCollider3DComponent>() &&
          !entity.component<ModelCollider3DComponent>()->isTrigger);
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
    if (!other.enabled || other.id == moving.id ||
        !other.hasComponent<Transform3DComponent>() ||
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

void stepPhysics3D(World &world, const float fixedDt, const Vec3 gravity) {
  ensurePhysicsWorld3D(world).step(world, fixedDt, gravity);
}

bool setRigidbodyVelocity3D(World &world, const std::string &entityId,
                            const Vec3 velocity) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr)
    return false;
  body->velocity = velocity;
  if (world.physicsWorld3D != nullptr)
    (void)world.physicsWorld3D->setVelocity(entityId, velocity);
  return true;
}

std::optional<Vec3> rigidbodyVelocity3D(const World &world,
                                        const std::string &entityId) {
  const Entity *entity = findEntity(world, entityId);
  const auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  return body != nullptr ? std::optional{body->velocity} : std::nullopt;
}

bool addRigidbodyForce3D(World &world, const std::string &entityId,
                         const Vec3 force) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr || body->bodyType != "dynamic")
    return false;
  body->accumulatedForce.x += force.x;
  body->accumulatedForce.y += force.y;
  body->accumulatedForce.z += force.z;
  return true;
}

bool addRigidbodyImpulse3D(World &world, const std::string &entityId,
                           const Vec3 impulse) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr || body->bodyType != "dynamic")
    return false;
  body->accumulatedImpulse.x += impulse.x;
  body->accumulatedImpulse.y += impulse.y;
  body->accumulatedImpulse.z += impulse.z;
  return true;
}

bool addRigidbodyTorque3D(World &world, const std::string &entityId,
                          const Vec3 torque) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr || body->bodyType != "dynamic")
    return false;
  body->accumulatedTorque.x += torque.x;
  body->accumulatedTorque.y += torque.y;
  body->accumulatedTorque.z += torque.z;
  return true;
}

bool setRigidbodyAwake3D(World &world, const std::string &entityId,
                         const bool awake) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr)
    return false;
  body->awake = awake;
  if (world.physicsWorld3D != nullptr)
    (void)world.physicsWorld3D->setAwake(entityId, awake);
  return true;
}

bool setRigidbodyEnabled3D(World &world, const std::string &entityId,
                           const bool enabled) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr)
    return false;
  body->bodyEnabled = enabled;
  if (world.physicsWorld3D != nullptr)
    (void)world.physicsWorld3D->setEnabled(entityId, enabled);
  return true;
}

bool moveKinematicBody3D(World &world, const std::string &entityId,
                         const Vec3 targetPosition, const Vec3 targetRotation,
                         const float fixedDt) {
  Entity *entity = findEntity(world, entityId);
  auto *body =
      entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (body == nullptr || body->bodyType != "kinematic" || fixedDt <= 0.0F)
    return false;
  body->kinematicTargetPosition = targetPosition;
  body->kinematicTargetRotation = targetRotation;
  body->kinematicTargetDt = fixedDt;
  body->hasKinematicTarget = true;
  return true;
}

std::vector<PhysicsContact3D>
contactsForEntity3D(const World &world, const std::string &entityId) {
  std::vector<PhysicsContact3D> result;
  std::ranges::copy_if(world.physicsContacts3D, std::back_inserter(result),
                       [&](const PhysicsContact3D &contact) {
                         return contact.entityId == entityId;
                       });
  return result;
}

bool setCharacterVelocity3D(World &world, const std::string &entityId,
                            const Vec3 velocity) {
  Entity *entity = findEntity(world, entityId);
  auto *controller =
      entity != nullptr
          ? entity->component<CharacterController3DComponent>()
          : nullptr;
  if (controller == nullptr)
    return false;
  controller->desiredVelocity = velocity;
  return true;
}

bool requestCharacterJump3D(World &world, const std::string &entityId,
                            const float speed) {
  Entity *entity = findEntity(world, entityId);
  auto *controller =
      entity != nullptr
          ? entity->component<CharacterController3DComponent>()
          : nullptr;
  if (controller == nullptr || speed <= 0.0F)
    return false;
  controller->requestedJumpSpeed =
      std::max(controller->requestedJumpSpeed, speed);
  return true;
}

std::optional<CharacterMoveResult3D>
characterState3D(const World &world, const std::string &entityId) {
  const Entity *entity = findEntity(world, entityId);
  const auto *controller =
      entity != nullptr
          ? entity->component<CharacterController3DComponent>()
          : nullptr;
  if (controller == nullptr)
    return std::nullopt;
  return CharacterMoveResult3D{
      .appliedMotion = controller->velocity,
      .remainingMotion = {},
      .grounded = controller->grounded,
      .groundEntity = controller->groundEntity,
  };
}

} // namespace demi::runtime
