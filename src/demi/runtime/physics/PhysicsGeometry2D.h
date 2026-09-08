#pragma once

#include <string>

namespace demi::runtime {

struct Entity;
struct Transform2DComponent;
struct Vec2;

} // namespace demi::runtime

namespace demi::runtime::physics2d_detail {

struct Aabb {
  float minX = 0.0F;
  float minY = 0.0F;
  float maxX = 0.0F;
  float maxY = 0.0F;
};

[[nodiscard]] Vec2 scaledLocalPoint(const Transform2DComponent &transform,
                                    Vec2 point);
[[nodiscard]] Vec2 absoluteScale(const Transform2DComponent &transform);
[[nodiscard]] float circleScale(const Transform2DComponent &transform);
[[nodiscard]] bool participatesInCollision(const Entity &entity);
[[nodiscard]] Aabb colliderAabb(const Entity &entity);
[[nodiscard]] std::string colliderLayer(const Entity &entity);
[[nodiscard]] bool hasCollider(const Entity &entity);
[[nodiscard]] bool queryIntersects(const Aabb &left, const Aabb &right);
[[nodiscard]] Aabb makeAabb(Vec2 center, Vec2 size);

} // namespace demi::runtime::physics2d_detail
