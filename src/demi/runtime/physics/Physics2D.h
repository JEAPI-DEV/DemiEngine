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
[[nodiscard]] bool overlapBox(const World &world, Vec2 center, Vec2 size,
                              const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsContact2D>
contactsForEntity(const World &world, const std::string &entityId);
[[nodiscard]] bool hasContact(const World &world, const std::string &entityId,
                              const PhysicsContactFilter2D &filter = {});
void stepPhysics2D(World &world, float fixedDt,
                   const PhysicsSettings2D &settings = {});

} // namespace demi::runtime
