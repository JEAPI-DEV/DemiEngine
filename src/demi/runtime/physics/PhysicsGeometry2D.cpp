#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/physics/Box2DShapes2D.h"

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
#include <box2d/box2d.h>

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

std::optional<Aabb> solidColliderAabb(const World &world,
                                      const Entity &entity) {
  const auto pose = resolveWorldTransform2D(world, entity);
  if (!pose) {
    return std::nullopt;
  }
  Transform2DComponent transform;
  transform.position = pose->position;
  transform.rotation = pose->rotation;
  transform.scale = pose->scale;
  const b2Transform nativeTransform({pose->position.x, pose->position.y},
                                    b2Rot(pose->rotation));
  std::optional<b2AABB> combined;
  visitBox2DFixtures2D(
      world, entity, transform, [&](const b2FixtureDef &fixture) {
        if (fixture.isSensor) {
          return;
        }
        for (int32 child = 0; child < fixture.shape->GetChildCount(); ++child) {
          b2AABB bounds;
          fixture.shape->ComputeAABB(&bounds, nativeTransform, child);
          // Movement clips to authored geometry, not the polygon/edge solver
          // skin. Circle radii are the physical surface and must remain in the
          // bounds.
          const float skin = fixture.shape->GetType() == b2Shape::e_circle
                                 ? 0.0F
                                 : fixture.shape->m_radius;
          bounds.lowerBound += b2Vec2(skin, skin);
          bounds.upperBound -= b2Vec2(skin, skin);
          if (combined) {
            combined->Combine(bounds);
          } else {
            combined = bounds;
          }
        }
      });
  if (!combined) {
    return std::nullopt;
  }
  return Aabb{combined->lowerBound.x, combined->lowerBound.y,
              combined->upperBound.x, combined->upperBound.y};
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
