#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>

#if DEMI_HAS_BOX2D
#include <box2d/box2d.h>
#endif

namespace demi::runtime {

using physics2d_detail::Aabb;
using physics2d_detail::colliderAabb;
using physics2d_detail::participatesInCollision;

namespace {

constexpr float KinematicContactSlop = 0.0001F;

} // namespace

std::optional<Vec2> rigidbodyVelocity(const World &world,
                                      const std::string &entityId) {
  const Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Rigidbody2DComponent>()->velocity;
}

bool setRigidbodyVelocity(World &world, const std::string &entityId,
                          const Vec2 velocity) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity = velocity;
  return true;
}

bool setRigidbodyVelocityX(World &world, const std::string &entityId,
                           const float x) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.x = x;
  return true;
}

bool setRigidbodyVelocityY(World &world, const std::string &entityId,
                           const float y) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.y = y;
  return true;
}

bool addRigidbodyImpulse(World &world, const std::string &entityId,
                         const Vec2 impulse) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.x += impulse.x;
  entity->component<Rigidbody2DComponent>()->velocity.y += impulse.y;
  return true;
}

bool addRigidbodyForce(World &world, const std::string &entityId,
                       const Vec2 force) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
#if DEMI_HAS_BOX2D
  if (world.box2dState != nullptr) {
    if (const auto found = world.box2dState->bodies.find(entityId);
        found != world.box2dState->bodies.end()) {
      auto *body = static_cast<b2Body *>(found->second);
      body->ApplyForceToCenter({force.x, force.y}, true);
      return true;
    }
  }
#endif
  entity->component<Rigidbody2DComponent>()->velocity.x += force.x / 60.0F;
  entity->component<Rigidbody2DComponent>()->velocity.y += force.y / 60.0F;
  return true;
}

bool addRigidbodyTorque(World &world, const std::string &entityId,
                        const float torque) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
#if DEMI_HAS_BOX2D
  if (world.box2dState != nullptr) {
    if (const auto found = world.box2dState->bodies.find(entityId);
        found != world.box2dState->bodies.end()) {
      static_cast<b2Body *>(found->second)->ApplyTorque(torque, true);
      return true;
    }
  }
#endif
  entity->component<Rigidbody2DComponent>()->angularVelocity += torque / 60.0F;
  return true;
}

bool setRigidbodyAngularVelocity(World &world, const std::string &entityId,
                                 const float angularVelocity) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->angularVelocity = angularVelocity;
  return true;
}

bool setRigidbodyAwake(World &world, const std::string &entityId,
                       const bool awake) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->awake = awake;
  return true;
}

bool setRigidbodyEnabled(World &world, const std::string &entityId,
                         const bool enabled) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->bodyEnabled = enabled;
  return true;
}

bool setRigidbodyContinuous(World &world, const std::string &entityId,
                            const bool continuous) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->continuous = continuous;
  return true;
}

bool setRigidbodyReportContacts(World &world, const std::string &entityId,
                                const bool reportContacts) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->reportContacts = reportContacts;
  return true;
}

bool moveKinematicBody(World &world, const std::string &entityId,
                       const Vec2 target, const float fixedDt) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>() ||
      !entity->hasComponent<Rigidbody2DComponent>() || fixedDt <= 0.0F)
    return false;
  Rigidbody2DComponent &body = *entity->component<Rigidbody2DComponent>();
  if (body.bodyType != "kinematic")
    return false;
  const Vec2 current = entity->component<Transform2DComponent>()->position;
  body.velocity = {(target.x - current.x) / fixedDt,
                   (target.y - current.y) / fixedDt};
  return true;
}

std::optional<Vec2> moveAndSlideKinematic(World &world,
                                          const std::string &entityId,
                                          const Vec2 motion) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>() ||
      !entity->hasComponent<Rigidbody2DComponent>() ||
      entity->component<Rigidbody2DComponent>()->bodyType != "kinematic" ||
      !participatesInCollision(*entity))
    return std::nullopt;

  Aabb bounds = colliderAabb(*entity);
  Vec2 applied = motion;
  const auto isStaticObstacle = [&entity](const Entity &candidate) {
    if (&candidate == entity || !candidate.enabled ||
        !participatesInCollision(candidate))
      return false;
    const auto *body = candidate.component<Rigidbody2DComponent>();
    return body == nullptr || body->bodyType == "static";
  };
  for (const Entity &candidate : world.entities) {
    if (!isStaticObstacle(candidate))
      continue;
    const Aabb obstacle = colliderAabb(candidate);
    if (bounds.maxY <= obstacle.minY || bounds.minY >= obstacle.maxY)
      continue;
    const float rightSeparation = obstacle.minX - bounds.maxX;
    const float leftSeparation = obstacle.maxX - bounds.minX;
    if (applied.x > 0.0F && rightSeparation >= -KinematicContactSlop &&
        applied.x > rightSeparation)
      applied.x = std::min(applied.x, rightSeparation);
    else if (applied.x < 0.0F && leftSeparation <= KinematicContactSlop &&
             applied.x < leftSeparation)
      applied.x = std::max(applied.x, leftSeparation);
  }
  bounds.minX += applied.x;
  bounds.maxX += applied.x;
  for (const Entity &candidate : world.entities) {
    if (!isStaticObstacle(candidate))
      continue;
    const Aabb obstacle = colliderAabb(candidate);
    if (bounds.maxX <= obstacle.minX || bounds.minX >= obstacle.maxX)
      continue;
    const float topSeparation = obstacle.minY - bounds.maxY;
    const float bottomSeparation = obstacle.maxY - bounds.minY;
    if (applied.y > 0.0F && topSeparation >= -KinematicContactSlop &&
        applied.y > topSeparation)
      applied.y = std::min(applied.y, topSeparation);
    else if (applied.y < 0.0F && bottomSeparation <= KinematicContactSlop &&
             applied.y < bottomSeparation)
      applied.y = std::max(applied.y, bottomSeparation);
  }

  Transform2DComponent &transform = *entity->component<Transform2DComponent>();
  transform.position.x += applied.x;
  transform.position.y += applied.y;
  entity->component<Rigidbody2DComponent>()->velocity = {};
  return applied;
}

} // namespace demi::runtime
