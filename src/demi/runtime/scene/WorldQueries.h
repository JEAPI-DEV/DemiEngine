#pragma once

#include "demi/runtime/scene/components/EngineComponents.h"

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
    if (entity.component<Transform2DComponent>() &&
        entity.component<SpriteComponent>())
      return &entity;
  }
  return nullptr;
}
[[nodiscard]] inline const Camera2DComponent *activeCamera(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.component<Camera2DComponent>())
      return &*entity.component<Camera2DComponent>();
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
  if (!entity.component<Transform2DComponent>())
    return {};
  Vec2 position = entity.component<Transform2DComponent>()->position;
  if (!entity.component<Transform2DComponent>()->parent.empty())
    if (const Entity *parent =
            findEntity(world, entity.component<Transform2DComponent>()->parent);
        parent != nullptr && parent->component<Transform2DComponent>()) {
      const Vec2 rotated = rotate2D(
          position, parent->component<Transform2DComponent>()->rotation);
      const Vec2 parentPosition = worldPosition2D(world, *parent);
      position = {.x = parentPosition.x + rotated.x,
                  .y = parentPosition.y + rotated.y};
    }
  return position;
}
[[nodiscard]] inline IsoTransformComponent
worldIsoTransform(const World &world, const Entity &entity) {
  const auto *transform = entity.component<IsoTransformComponent>();
  if (transform == nullptr)
    return {};
  IsoTransformComponent resolved = *transform;
  if (!transform->parent.empty()) {
    const Entity *parent = findEntity(world, transform->parent);
    if (parent != nullptr && parent->hasComponent<IsoTransformComponent>()) {
      const IsoTransformComponent parentTransform =
          worldIsoTransform(world, *parent);
      resolved.tile.x += parentTransform.tile.x;
      resolved.tile.y += parentTransform.tile.y;
      resolved.height += parentTransform.height;
    }
  }
  return resolved;
}
[[nodiscard]] inline float worldRotation2D(const World &world,
                                           const Entity &entity) {
  if (!entity.component<Transform2DComponent>())
    return 0.0F;
  float rotation = entity.component<Transform2DComponent>()->rotation;
  if (!entity.component<Transform2DComponent>()->parent.empty())
    if (const Entity *parent =
            findEntity(world, entity.component<Transform2DComponent>()->parent);
        parent != nullptr && parent->component<Transform2DComponent>())
      rotation += worldRotation2D(world, *parent);
  return rotation;
}
[[nodiscard]] inline Vec2 activeCameraPosition(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.component<Camera2DComponent>() &&
        entity.component<Transform2DComponent>())
      return worldPosition2D(world, entity);
  return {};
}
[[nodiscard]] inline const Camera3DComponent *
activeCamera3D(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.component<Camera3DComponent>())
      return &*entity.component<Camera3DComponent>();
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
  if (!entity.component<Transform3DComponent>())
    return {};
  Vec3 position = entity.component<Transform3DComponent>()->position;
  if (!entity.component<Transform3DComponent>()->parent.empty())
    if (const Entity *parent =
            findEntity(world, entity.component<Transform3DComponent>()->parent);
        parent != nullptr && parent->component<Transform3DComponent>()) {
      const Vec3 rotated = rotateYaw(
          position, parent->component<Transform3DComponent>()->rotation.y);
      const Vec3 parentPosition = worldPosition3D(world, *parent);
      position = {.x = parentPosition.x + rotated.x,
                  .y = parentPosition.y + rotated.y,
                  .z = parentPosition.z + rotated.z};
    }
  return position;
}
[[nodiscard]] inline Vec3 worldRotation3D(const World &world,
                                          const Entity &entity) {
  if (!entity.component<Transform3DComponent>())
    return {};
  Vec3 rotation = entity.component<Transform3DComponent>()->rotation;
  if (!entity.component<Transform3DComponent>()->parent.empty())
    if (const Entity *parent =
            findEntity(world, entity.component<Transform3DComponent>()->parent);
        parent != nullptr && parent->component<Transform3DComponent>()) {
      const Vec3 parentRotation = worldRotation3D(world, *parent);
      rotation.x += parentRotation.x;
      rotation.y += parentRotation.y;
      rotation.z += parentRotation.z;
    }
  return rotation;
}
[[nodiscard]] inline Vec3 activeCamera3DPosition(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.component<Camera3DComponent>() &&
        entity.component<Transform3DComponent>())
      return worldPosition3D(world, entity);
  return {};
}
[[nodiscard]] inline Vec3 activeCamera3DRotation(const World &world) {
  for (const Entity &entity : world.entities)
    if (entity.component<Camera3DComponent>() &&
        entity.component<Transform3DComponent>())
      return worldRotation3D(world, entity);
  return {};
}
[[nodiscard]] inline bool sceneIs3D(const World &world) {
  return activeCamera3D(world) != nullptr;
}
[[nodiscard]] inline std::size_t renderableEntityCount(const World &world) {
  std::size_t count = 0;
  for (const Entity &entity : world.entities)
    if (entity.component<SpriteComponent>() ||
        entity.component<Tilemap2DComponent>() ||
        entity.component<IsoGridComponent>() ||
        entity.component<BuildableComponent>() ||
        entity.component<BoxCollider2DComponent>() ||
        entity.component<CircleCollider2DComponent>() ||
        entity.component<VideoPlayerComponent>() ||
        entity.component<MeshRendererComponent>() ||
        entity.component<BoxCollider3DComponent>() ||
        entity.component<SphereCollider3DComponent>())
      ++count;
  return count;
}

} // namespace demi::runtime
