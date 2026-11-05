#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct Entity;
struct World;
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
[[nodiscard]] std::optional<Aabb> colliderAabb(const World &world,
                                               const Entity &entity);
[[nodiscard]] std::vector<std::size_t>
physicsEntityIndices2D(const World &world);
[[nodiscard]] std::string colliderLayer(const Entity &entity);
[[nodiscard]] bool hasCollider(const Entity &entity);

} // namespace demi::runtime::physics2d_detail
