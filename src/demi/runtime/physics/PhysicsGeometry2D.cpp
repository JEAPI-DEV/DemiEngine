#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/Transform2DHierarchy.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CapsuleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/EdgeCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/PolygonCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace demi::runtime::physics2d_detail {

[[nodiscard]] Vec2 scaledLocalPoint(const Transform2DComponent &transform,
                                    const Vec2 point) {
  return {.x = point.x * transform.scale.x, .y = point.y * transform.scale.y};
}

[[nodiscard]] Vec2 absoluteScale(const Transform2DComponent &transform) {
  return {.x = std::abs(transform.scale.x), .y = std::abs(transform.scale.y)};
}

[[nodiscard]] float circleScale(const Transform2DComponent &transform) {
  const Vec2 scale = absoluteScale(transform);
  return std::max(scale.x, scale.y);
}

[[nodiscard]] bool participatesInCollision(const Entity &entity) {
  if (!entity.hasComponent<Transform2DComponent>())
    return false;
  if (const auto *box = entity.component<BoxCollider2DComponent>())
    return !box->isTrigger;
  if (const auto *circle = entity.component<CircleCollider2DComponent>())
    return !circle->isTrigger;
  if (const auto *capsule = entity.component<CapsuleCollider2DComponent>())
    return !capsule->isTrigger;
  if (const auto *polygon = entity.component<PolygonCollider2DComponent>())
    return !polygon->isTrigger;
  if (const auto *edge = entity.component<EdgeCollider2DComponent>())
    return !edge->isTrigger;
  return false;
}

std::optional<Aabb> colliderAabb(const World &world, const Entity &entity) {
  const auto pose = resolveWorldTransform2D(world, entity);
  if (!pose) {
    return std::nullopt;
  }
  Transform2DComponent transform;
  transform.position = pose->position;
  transform.rotation = pose->rotation;
  transform.scale = pose->scale;
  const float cosine = std::cos(transform.rotation);
  const float sine = std::sin(transform.rotation);
  const auto worldPoint = [&](const Vec2 point) {
    const Vec2 scaled = scaledLocalPoint(transform, point);
    return Vec2{transform.position.x + scaled.x * cosine - scaled.y * sine,
                transform.position.y + scaled.x * sine + scaled.y * cosine};
  };
  if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
    const Vec2 center = worldPoint(circle->offset);
    const float radius = circle->radius * circleScale(transform);
    return Aabb{center.x - radius, center.y - radius, center.x + radius,
                center.y + radius};
  }

  std::vector<Vec2> points;
  if (const auto *box = entity.component<BoxCollider2DComponent>()) {
    const Vec2 half{box->size.x * 0.5F, box->size.y * 0.5F};
    points = {{box->offset.x - half.x, box->offset.y - half.y},
              {box->offset.x + half.x, box->offset.y - half.y},
              {box->offset.x + half.x, box->offset.y + half.y},
              {box->offset.x - half.x, box->offset.y + half.y}};
  } else if (const auto *capsule =
                 entity.component<CapsuleCollider2DComponent>()) {
    const Vec2 half{capsule->size.x * 0.5F, capsule->size.y * 0.5F};
    points = {{capsule->offset.x - half.x, capsule->offset.y - half.y},
              {capsule->offset.x + half.x, capsule->offset.y - half.y},
              {capsule->offset.x + half.x, capsule->offset.y + half.y},
              {capsule->offset.x - half.x, capsule->offset.y + half.y}};
  } else if (const auto *polygon =
                 entity.component<PolygonCollider2DComponent>()) {
    points.reserve(polygon->points.size());
    for (const Vec2 point : polygon->points)
      points.push_back(
          {point.x + polygon->offset.x, point.y + polygon->offset.y});
  } else if (const auto *edge = entity.component<EdgeCollider2DComponent>()) {
    points = edge->points;
  }
  if (points.empty())
    return Aabb{transform.position.x, transform.position.y,
                transform.position.x, transform.position.y};
  const Vec2 first = worldPoint(points.front());
  Aabb result{first.x, first.y, first.x, first.y};
  for (const Vec2 point : points) {
    const Vec2 world = worldPoint(point);
    result.minX = std::min(result.minX, world.x);
    result.minY = std::min(result.minY, world.y);
    result.maxX = std::max(result.maxX, world.x);
    result.maxY = std::max(result.maxY, world.y);
  }
  return result;
}

std::vector<std::size_t> physicsEntityIndices2D(const World &world) {
  auto indices =
      world.componentQueries
          .queryAny<Rigidbody2DComponent, BoxCollider2DComponent,
                    CircleCollider2DComponent, CapsuleCollider2DComponent,
                    PolygonCollider2DComponent, EdgeCollider2DComponent>(
              world.entities);
  std::erase_if(indices, [&](std::size_t index) {
    return !world.entities[index].hasComponent<Transform2DComponent>();
  });
  return indices;
}

[[nodiscard]] std::string colliderLayer(const Entity &entity) {
  if (const auto *box = entity.component<BoxCollider2DComponent>())
    return box->layer;
  if (const auto *circle = entity.component<CircleCollider2DComponent>())
    return circle->layer;
  if (const auto *capsule = entity.component<CapsuleCollider2DComponent>())
    return capsule->layer;
  if (const auto *polygon = entity.component<PolygonCollider2DComponent>())
    return polygon->layer;
  if (const auto *edge = entity.component<EdgeCollider2DComponent>())
    return edge->layer;
  return {};
}

[[nodiscard]] bool hasCollider(const Entity &entity) {
  return entity.hasComponent<BoxCollider2DComponent>() ||
         entity.hasComponent<CircleCollider2DComponent>() ||
         entity.hasComponent<CapsuleCollider2DComponent>() ||
         entity.hasComponent<PolygonCollider2DComponent>() ||
         entity.hasComponent<EdgeCollider2DComponent>();
}

} // namespace demi::runtime::physics2d_detail
