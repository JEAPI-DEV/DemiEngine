#pragma once

#include "demi/runtime/scene/model/World.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct PhysicsSettings2D {
  Vec2 gravity = {0.0F, -18.0F};
};

struct PhysicsContactFilter2D {
  std::optional<std::string> layer;
  std::optional<float> normalXMin;
  std::optional<float> normalXMax;
  std::optional<float> normalYMin;
  std::optional<float> normalYMax;
  bool includeTriggers = false;
};

struct PhysicsRaycastHit2D {
  std::string entityId;
  std::string layer;
  Vec2 point;
  Vec2 normal;
  float distance = 0.0F;
  float fraction = 0.0F;
};
using PhysicsQueryHit2D = PhysicsRaycastHit2D;

[[nodiscard]] std::optional<Vec2>
rigidbodyVelocity(const World &world, const std::string &entityId);
[[nodiscard]] bool
setRigidbodyVelocity(World &world, const std::string &entityId, Vec2 velocity);
[[nodiscard]] bool setRigidbodyVelocityX(World &world,
                                         const std::string &entityId, float x);
[[nodiscard]] bool setRigidbodyVelocityY(World &world,
                                         const std::string &entityId, float y);
[[nodiscard]] bool
addRigidbodyImpulse(World &world, const std::string &entityId, Vec2 impulse);
[[nodiscard]] bool addRigidbodyForce(World &world, const std::string &entityId,
                                     Vec2 force);
[[nodiscard]] bool addRigidbodyTorque(World &world, const std::string &entityId,
                                      float torque);
[[nodiscard]] bool setRigidbodyAngularVelocity(World &world,
                                               const std::string &entityId,
                                               float angularVelocity);
[[nodiscard]] bool setRigidbodyAwake(World &world, const std::string &entityId,
                                     bool awake);
[[nodiscard]] bool
setRigidbodyEnabled(World &world, const std::string &entityId, bool enabled);
[[nodiscard]] bool moveKinematicBody(World &world, const std::string &entityId,
                                     Vec2 target, float fixedDt);
[[nodiscard]] std::optional<Vec2>
moveAndSlideKinematic(World &world, const std::string &entityId, Vec2 motion);
[[nodiscard]] bool overlapBox(const World &world, Vec2 center, Vec2 size,
                              const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<std::string>
overlapCircle(const World &world, Vec2 center, float radius,
              const std::string &layer = {},
              const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsQueryHit2D>
overlapBoxAll(const World &world, Vec2 center, Vec2 size,
              const std::string &layer = {},
              const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsQueryHit2D>
overlapCircleAll(const World &world, Vec2 center, float radius,
                 const std::string &layer = {},
                 const std::string &ignoredEntityId = {});
[[nodiscard]] std::optional<PhysicsRaycastHit2D>
raycast2D(const World &world, Vec2 origin, Vec2 direction, float distance,
          const std::string &layer = {},
          const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsContact2D>
contactsForEntity(const World &world, const std::string &entityId);
[[nodiscard]] bool hasContact(const World &world, const std::string &entityId,
                              const PhysicsContactFilter2D &filter = {});
void stepPhysics2D(World &world, float fixedDt,
                   const PhysicsSettings2D &settings = {});

} // namespace demi::runtime
