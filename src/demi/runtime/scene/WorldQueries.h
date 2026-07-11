#pragma once

#include "demi/runtime/scene/model/World.h"

#include <cmath>

namespace demi::runtime {

[[nodiscard]] inline Entity *findEntity(World &world, const std::string &id) {
  for (Entity &entity : world.entities) {
    if (entity.id == id)
      return &entity;
  }
  return nullptr;
}
[[nodiscard]] inline const Entity *findEntity(const World &world,
                                              const std::string &id) {
  for (const Entity &entity : world.entities) {
    if (entity.id == id)
      return &entity;
  }
  return nullptr;
}
[[nodiscard]] inline Entity *findFirstControllableEntity(World &world) {
  if (Entity *player = findEntity(world, "ent_player"))
    return player;
  for (Entity &entity : world.entities) {
    if (entity.transform2D && entity.sprite)
      return &entity;
  }
  return nullptr;
}
[[nodiscard]] inline const Camera2DComponent *activeCamera(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.camera2D)
      return &*entity.camera2D;
  return nullptr;
}
[[nodiscard]] inline Vec2 rotate2D(const Vec2 value, const float rotation) {
  const float sinR = std::sin(rotation);
  const float cosR = std::cos(rotation);
  return {.x = value.x * cosR - value.y * sinR,
          .y = value.x * sinR + value.y * cosR};
}
[[nodiscard]] inline Vec2 worldPosition2D(const World &world,
                                          const Entity &entity) {
  if (!entity.transform2D)
    return {};
  Vec2 position = entity.transform2D->position;
  if (!entity.transform2D->parent.empty())
    if (const Entity *parent = findEntity(world, entity.transform2D->parent);
        parent != nullptr && parent->transform2D) {
      const Vec2 rotated = rotate2D(position, parent->transform2D->rotation);
      const Vec2 parentPosition = worldPosition2D(world, *parent);
      position = {.x = parentPosition.x + rotated.x,
                  .y = parentPosition.y + rotated.y};
    }
  return position;
}
[[nodiscard]] inline float worldRotation2D(const World &world,
                                           const Entity &entity) {
  if (!entity.transform2D)
    return 0.0F;
  float rotation = entity.transform2D->rotation;
  if (!entity.transform2D->parent.empty())
    if (const Entity *parent = findEntity(world, entity.transform2D->parent);
        parent != nullptr && parent->transform2D)
      rotation += worldRotation2D(world, *parent);
  return rotation;
}
[[nodiscard]] inline Vec2 activeCameraPosition(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.camera2D && entity.transform2D)
      return worldPosition2D(world, entity);
  return {};
}
[[nodiscard]] inline const Camera3DComponent *
activeCamera3D(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.camera3D)
      return &*entity.camera3D;
  return nullptr;
}
[[nodiscard]] inline Vec3 rotateYaw(const Vec3 value, const float yaw) {
  const float sinY = std::sin(yaw);
  const float cosY = std::cos(yaw);
  return {.x = value.x * cosY + value.z * sinY,
          .y = value.y,
          .z = -value.x * sinY + value.z * cosY};
}
[[nodiscard]] inline Vec3 worldPosition3D(const World &world,
                                          const Entity &entity) {
  if (!entity.transform3D)
    return {};
  Vec3 position = entity.transform3D->position;
  if (!entity.transform3D->parent.empty())
    if (const Entity *parent = findEntity(world, entity.transform3D->parent);
        parent != nullptr && parent->transform3D) {
      const Vec3 rotated = rotateYaw(position, parent->transform3D->rotation.y);
      const Vec3 parentPosition = worldPosition3D(world, *parent);
      position = {.x = parentPosition.x + rotated.x,
                  .y = parentPosition.y + rotated.y,
                  .z = parentPosition.z + rotated.z};
    }
  return position;
}
[[nodiscard]] inline Vec3 worldRotation3D(const World &world,
                                          const Entity &entity) {
  if (!entity.transform3D)
    return {};
  Vec3 rotation = entity.transform3D->rotation;
  if (!entity.transform3D->parent.empty())
    if (const Entity *parent = findEntity(world, entity.transform3D->parent);
        parent != nullptr && parent->transform3D) {
      const Vec3 parentRotation = worldRotation3D(world, *parent);
      rotation.x += parentRotation.x;
      rotation.y += parentRotation.y;
      rotation.z += parentRotation.z;
    }
  return rotation;
}
[[nodiscard]] inline Vec3 activeCamera3DPosition(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.camera3D && entity.transform3D)
      return worldPosition3D(world, entity);
  return {};
}
[[nodiscard]] inline Vec3 activeCamera3DRotation(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.camera3D && entity.transform3D)
      return worldRotation3D(world, entity);
  return {};
}
[[nodiscard]] inline bool sceneIs3D(const World &world) {
  return activeCamera3D(world) != nullptr;
}
[[nodiscard]] inline std::size_t renderableEntityCount(const World &world) {
  std::size_t count = 0;
  for (const Entity &entity : world.entities)
    if (entity.sprite || entity.hitboxController || entity.isoGrid ||
        entity.buildable || entity.boxCollider2D || entity.videoPlayer ||
        entity.meshRenderer || entity.boxCollider3D || entity.sphereCollider3D)
      ++count;
  return count;
}

} // namespace demi::runtime
