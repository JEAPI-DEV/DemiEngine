#include "demi/runtime/physics/Box2DShapes2D.h"

#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <vector>

namespace demi::runtime {
void visitBox2DFixtures2D(
    const World &world, const Entity &entity,
    const Transform2DComponent &transform,
    const std::function<void(const b2FixtureDef &)> &visitor) {
  using physics2d_detail::scaledLocalPoint;
  const Vec2 scale = physics2d_detail::absoluteScale(transform);
  const auto publish = [&](const b2Shape &shape, const auto &collider,
                           bool inheritBounciness = false) {
    b2FixtureDef definition;
    definition.shape = &shape;
    definition.density = collider.density;
    definition.friction = collider.friction;
    definition.restitution = collider.restitution;
    if (inheritBounciness && definition.restitution <= 0.0F) {
      if (const auto *body = entity.component<Rigidbody2DComponent>()) {
        definition.restitution = std::clamp(body->bounciness, 0.0F, 1.0F);
      }
    }
    definition.isSensor = collider.isTrigger;
    definition.filter.categoryBits = collider.categoryBits;
    definition.filter.maskBits = collider.maskBits;
    if (const auto category = world.physicsCategoryBits.find(collider.layer);
        category != world.physicsCategoryBits.end()) {
      definition.filter.categoryBits = category->second;
      definition.filter.maskBits = world.physicsMaskBits.at(collider.layer);
    }
    visitor(definition);
  };
  if (const auto *box = entity.component<BoxCollider2DComponent>()) {
    const Vec2 offset = scaledLocalPoint(transform, box->offset);
    b2PolygonShape shape;
    shape.SetAsBox(std::max(box->size.x * scale.x * 0.5F, b2_epsilon),
                   std::max(box->size.y * scale.y * 0.5F, b2_epsilon),
                   {offset.x, offset.y}, 0.0F);
    publish(shape, *box, true);
  }
  if (const auto *circle = entity.component<CircleCollider2DComponent>()) {
    const Vec2 offset = scaledLocalPoint(transform, circle->offset);
    b2CircleShape shape;
    shape.m_p.Set(offset.x, offset.y);
    shape.m_radius = circle->radius * physics2d_detail::circleScale(transform);
    publish(shape, *circle, true);
  }
  if (const auto *capsule = entity.component<CapsuleCollider2DComponent>()) {
    const Vec2 size{capsule->size.x * scale.x, capsule->size.y * scale.y};
    const Vec2 offset = scaledLocalPoint(transform, capsule->offset);
    const float radius = std::max(std::min(size.x, size.y) * 0.5F, 0.001F);
    const bool vertical = size.y >= size.x;
    const float straight =
        std::max((vertical ? size.y : size.x) - 2 * radius, 0.0F);
    if (straight > 0.0F) {
      b2PolygonShape middle;
      middle.SetAsBox(vertical ? radius : straight * 0.5F,
                      vertical ? straight * 0.5F : radius, {offset.x, offset.y},
                      0.0F);
      publish(middle, *capsule);
    }
    for (const float side : {-1.0F, 1.0F}) {
      b2CircleShape cap;
      cap.m_radius = radius;
      cap.m_p = {offset.x + (vertical ? 0.0F : side * straight * 0.5F),
                 offset.y + (vertical ? side * straight * 0.5F : 0.0F)};
      publish(cap, *capsule);
    }
  }
  if (const auto *polygon = entity.component<PolygonCollider2DComponent>();
      polygon != nullptr && polygon->points.size() >= 3) {
    std::vector<b2Vec2> vertices;
    const auto count =
        std::min<std::size_t>(polygon->points.size(), b2_maxPolygonVertices);
    vertices.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const Vec2 point = scaledLocalPoint(
          transform, {polygon->points[index].x + polygon->offset.x,
                      polygon->points[index].y + polygon->offset.y});
      vertices.push_back({point.x, point.y});
    }
    b2PolygonShape shape;
    shape.Set(vertices.data(), static_cast<int32>(vertices.size()));
    publish(shape, *polygon);
  }
  if (const auto *edge = entity.component<EdgeCollider2DComponent>();
      edge != nullptr && edge->points.size() >= 2) {
    std::vector<b2Vec2> vertices;
    vertices.reserve(edge->points.size());
    for (const Vec2 point : edge->points) {
      const Vec2 scaled = scaledLocalPoint(transform, point);
      vertices.push_back({scaled.x, scaled.y});
    }
    b2ChainShape shape;
    if (edge->loop && vertices.size() >= 3) {
      shape.CreateLoop(vertices.data(), static_cast<int32>(vertices.size()));
    } else {
      shape.CreateChain(vertices.data(), static_cast<int32>(vertices.size()),
                        vertices.front(), vertices.back());
    }
    publish(shape, *edge);
  }
}
} // namespace demi::runtime
