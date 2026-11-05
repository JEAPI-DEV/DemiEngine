#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>

#include <box2d/box2d.h>

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
  // Keep the requested state coherent: native velocity changes wake Box2D,
  // so a stale copied sleeping flag must not put it back to sleep in sync.
  entity->component<Rigidbody2DComponent>()->velocity = velocity;
  if (velocity.x != 0.0F || velocity.y != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
  return true;
}

bool setRigidbodyVelocityX(World &world, const std::string &entityId,
                           const float x) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.x = x;
  if (x != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
  return true;
}

bool setRigidbodyVelocityY(World &world, const std::string &entityId,
                           const float y) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  entity->component<Rigidbody2DComponent>()->velocity.y = y;
  if (y != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
  return true;
}

bool addRigidbodyImpulse(World &world, const std::string &entityId,
                         const Vec2 impulse) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>()) {
    return false;
  }
  if (impulse.x != 0.0F || impulse.y != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
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
  if (force.x != 0.0F || force.y != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
  if (world.box2dState != nullptr) {
    if (const auto found = world.box2dState->bodies.find(entityId);
        found != world.box2dState->bodies.end()) {
      auto *body = static_cast<b2Body *>(found->second);
      body->ApplyForceToCenter({force.x, force.y}, true);
      return true;
    }
  }
  entity->component<Rigidbody2DComponent>()->velocity.x += force.x / 60.0F;
  entity->component<Rigidbody2DComponent>()->velocity.y += force.y / 60.0F;
  return true;
}

bool addRigidbodyTorque(World &world, const std::string &entityId,
                        const float torque) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  if (torque != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
  if (world.box2dState != nullptr) {
    if (const auto found = world.box2dState->bodies.find(entityId);
        found != world.box2dState->bodies.end()) {
      static_cast<b2Body *>(found->second)->ApplyTorque(torque, true);
      return true;
    }
  }
  entity->component<Rigidbody2DComponent>()->angularVelocity += torque / 60.0F;
  return true;
}

bool setRigidbodyAngularVelocity(World &world, const std::string &entityId,
                                 const float angularVelocity) {
  Entity *entity = findEntity(world, entityId);
  if (entity == nullptr || !entity->hasComponent<Rigidbody2DComponent>())
    return false;
  entity->component<Rigidbody2DComponent>()->angularVelocity = angularVelocity;
  if (angularVelocity != 0.0F) {
    entity->component<Rigidbody2DComponent>()->awake = true;
  }
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
  const auto pose = resolveWorldTransform2D(world, *entity);
  if (!pose) {
    return false;
  }
  const Vec2 current = pose->position;
  if (target.x != current.x || target.y != current.y) {
    body.awake = true;
  }
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

  const auto initialBounds = colliderAabb(world, *entity);
  auto pose = resolveWorldTransform2D(world, *entity);
  if (!initialBounds || !pose) {
    return std::nullopt;
  }
  Aabb bounds = *initialBounds;
  Vec2 applied = motion;
  const auto isStaticObstacle = [&entity](const Entity &candidate) {
    if (&candidate == entity || !candidate.enabled ||
        !participatesInCollision(candidate))
      return false;
    const auto *body = candidate.component<Rigidbody2DComponent>();
    return body == nullptr || body->bodyType == "static";
  };
  std::vector<Aabb> obstacles;
  for (const auto index : physics2d_detail::physicsEntityIndices2D(world)) {
    const Entity &candidate = world.entities[index];
    if (isStaticObstacle(candidate)) {
      if (const auto obstacle = colliderAabb(world, candidate)) {
        obstacles.push_back(*obstacle);
      }
    }
  }
  for (const Aabb &obstacle : obstacles) {
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
  for (const Aabb &obstacle : obstacles) {
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

  const auto &transform = *entity->component<Transform2DComponent>();
  pose->position.x += applied.x;
  pose->position.y += applied.y;
  const Entity *parent = findEntity(world, transform.parent);
  const auto parentPose =
      parent ? resolveWorldTransform2D(world, *parent) : std::nullopt;
  const auto local = localTransform2D(transform, *pose, parentPose);
  entity->updateComponent<Transform2DComponent>(
      [&](auto &updated) { updated.position = local.position; });
  entity->component<Rigidbody2DComponent>()->velocity = {};
  return applied;
}

} // namespace demi::runtime
