#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include "demi/runtime/scene/model/World.h"

#include <cmath>
#include <algorithm>
#include <vector>

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
  const Entity *best = nullptr;
  for (const Entity &entity : world.entities) {
    const auto *camera = entity.component<Camera3DComponent>();
    if (!entity.enabled || camera == nullptr)
      continue;
    const auto *bestCamera =
        best != nullptr ? best->component<Camera3DComponent>() : nullptr;
    if (best == nullptr || (camera->primary && !bestCamera->primary) ||
        (camera->primary == bestCamera->primary &&
        camera->priority >
            bestCamera->priority) ||
        (camera->primary == bestCamera->primary &&
         camera->priority == bestCamera->priority && entity.id < best->id))
      best = &entity;
  }
  return best != nullptr ? best->component<Camera3DComponent>() : nullptr;
}
[[nodiscard]] inline const Entity *activeCamera3DEntity(const World &world) {
  const Entity *best = nullptr;
  for (const Entity &entity : world.entities) {
    const auto *camera = entity.component<Camera3DComponent>();
    if (!entity.enabled || camera == nullptr ||
        !entity.hasComponent<Transform3DComponent>())
      continue;
    const auto *bestCamera =
        best != nullptr ? best->component<Camera3DComponent>() : nullptr;
    if (best == nullptr || (camera->primary && !bestCamera->primary) ||
        (camera->primary == bestCamera->primary &&
        camera->priority >
            bestCamera->priority) ||
        (camera->primary == bestCamera->primary &&
         camera->priority == bestCamera->priority && entity.id < best->id))
      best = &entity;
  }
  return best;
}
[[nodiscard]] inline std::vector<const Entity *>
renderCameras3D(const World &world) {
  std::vector<const Entity *> cameras;
  for (const Entity &entity : world.entities)
    if (entity.enabled && entity.hasComponent<Camera3DComponent>() &&
        entity.hasComponent<Transform3DComponent>())
      cameras.push_back(&entity);
  std::ranges::stable_sort(cameras,
                           [](const Entity *left, const Entity *right) {
    const auto *a = left->component<Camera3DComponent>();
    const auto *b = right->component<Camera3DComponent>();
    return a->priority != b->priority ? a->priority < b->priority
                                     : left->id < right->id;
  });
  return cameras;
}
[[nodiscard]] inline Vec3 worldPosition3D(const World &world,
                                          const Entity &entity) {
  const auto transform = resolveWorldTransform3D(world, entity);
  return transform ? transform->position : Vec3{};
}
[[nodiscard]] inline Vec3 worldRotation3D(const World &world,
                                          const Entity &entity) {
  const auto transform = resolveWorldTransform3D(world, entity);
  return transform ? transform->rotation : Vec3{};
}
[[nodiscard]] inline Vec3 worldScale3D(const World &world,
                                       const Entity &entity) {
  const auto transform = resolveWorldTransform3D(world, entity);
  return transform ? transform->scale : Vec3{1.0F, 1.0F, 1.0F};
}
[[nodiscard]] inline Vec3 activeCamera3DPosition(const World &world) {
  const Entity *camera = activeCamera3DEntity(world);
  return camera != nullptr ? worldPosition3D(world, *camera) : Vec3{};
}
[[nodiscard]] inline Vec3 activeCamera3DForward(const World &world) {
  const Entity *camera = activeCamera3DEntity(world);
  if (camera == nullptr)
    return {0.0F, 0.0F, 1.0F};
  const auto transform = resolveWorldTransform3D(world, *camera);
  const auto &component = *camera->component<Camera3DComponent>();
  return transform ? transformDirection3D(*transform, component.targetOffset)
                   : component.targetOffset;
}
[[nodiscard]] inline Vec3 activeCamera3DUp(const World &world) {
  const Entity *camera = activeCamera3DEntity(world);
  if (camera == nullptr)
    return {0.0F, 1.0F, 0.0F};
  const auto transform = resolveWorldTransform3D(world, *camera);
  const auto &component = *camera->component<Camera3DComponent>();
  const Vec3 localUp{0.0F, component.upAxis, 0.0F};
  return transform ? transformDirection3D(*transform, localUp) : localUp;
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
