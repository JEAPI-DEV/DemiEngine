#pragma once

#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace demi::runtime {

inline constexpr float PhysicsPresentationPoseEpsilon2D = 0.00001F;

[[nodiscard]] inline bool physicsPresentationSamePosition2D(const Vec2 left,
                                                            const Vec2 right) {
  return std::abs(left.x - right.x) <= PhysicsPresentationPoseEpsilon2D &&
         std::abs(left.y - right.y) <= PhysicsPresentationPoseEpsilon2D;
}

// These queries affect rendering only. Simulation and gameplay continue to
// consume the authoritative Transform2D pose written by the fixed physics step.
[[nodiscard]] inline std::optional<Vec2>
physicsPresentationPosition2D(const World &world, const Entity &entity,
                              const float alpha) {
  const auto *transform = entity.component<Transform2DComponent>();
  const auto found = world.physicsPresentationPoses2D.find(entity.id);
  if (transform == nullptr || found == world.physicsPresentationPoses2D.end())
    return std::nullopt;
  const PhysicsPresentationPose2D &pose = found->second;
  if (!physicsPresentationSamePosition2D(transform->position,
                                         pose.currentPosition))
    return transform->position;
  const float amount = std::clamp(alpha, 0.0F, 1.0F);
  return Vec2{
      .x = pose.previousPosition.x +
           (pose.currentPosition.x - pose.previousPosition.x) * amount,
      .y = pose.previousPosition.y +
           (pose.currentPosition.y - pose.previousPosition.y) * amount,
  };
}

[[nodiscard]] inline std::optional<float>
physicsPresentationRotation2D(const World &world, const Entity &entity,
                              const float alpha) {
  const auto *transform = entity.component<Transform2DComponent>();
  const auto found = world.physicsPresentationPoses2D.find(entity.id);
  if (transform == nullptr || found == world.physicsPresentationPoses2D.end())
    return std::nullopt;
  const PhysicsPresentationPose2D &pose = found->second;
  if (std::abs(transform->rotation - pose.currentRotation) >
      PhysicsPresentationPoseEpsilon2D)
    return transform->rotation;
  constexpr float Tau = 6.28318530717958647692F;
  const float delta =
      std::remainder(pose.currentRotation - pose.previousRotation, Tau);
  return pose.previousRotation + delta * std::clamp(alpha, 0.0F, 1.0F);
}

[[nodiscard]] inline float
physicsPresentationWorldRotation2D(const World &world, const Entity &entity,
                                   const float alpha) {
  const auto *transform = entity.component<Transform2DComponent>();
  if (transform == nullptr)
    return 0.0F;
  float rotation = physicsPresentationRotation2D(world, entity, alpha)
                       .value_or(transform->rotation);
  if (!transform->parent.empty()) {
    const Entity *parent = findEntity(world, transform->parent);
    if (parent != nullptr && parent->hasComponent<Transform2DComponent>())
      rotation += physicsPresentationWorldRotation2D(world, *parent, alpha);
  }
  return rotation;
}

[[nodiscard]] inline Vec2
physicsPresentationWorldPosition2D(const World &world, const Entity &entity,
                                   const float alpha) {
  const auto *transform = entity.component<Transform2DComponent>();
  if (transform == nullptr)
    return {};
  Vec2 position = physicsPresentationPosition2D(world, entity, alpha)
                      .value_or(transform->position);
  if (transform->parent.empty())
    return position;
  const Entity *parent = findEntity(world, transform->parent);
  if (parent == nullptr || !parent->hasComponent<Transform2DComponent>())
    return position;
  const Vec2 parentScale = worldScale2D(world, *parent);
  const Vec2 rotated = rotate2D(
      {.x = position.x * parentScale.x, .y = position.y * parentScale.y},
      physicsPresentationWorldRotation2D(world, *parent, alpha));
  const Vec2 parentPosition =
      physicsPresentationWorldPosition2D(world, *parent, alpha);
  return {.x = parentPosition.x + rotated.x, .y = parentPosition.y + rotated.y};
}

} // namespace demi::runtime
