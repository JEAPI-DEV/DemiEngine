#pragma once

#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <optional>
#include <span>
#include <vector>

namespace demi::runtime {
struct Entity;
struct World;

struct WorldTransform2D {
  Vec2 position;
  float rotation = 0.0F;
  Vec2 scale = {1.0F, 1.0F};
};

[[nodiscard]] WorldTransform2D
composeWorldTransform2D(const WorldTransform2D &parent,
                        const WorldTransform2D &local);
[[nodiscard]] Vec2 transformPoint2D(const WorldTransform2D &transform,
                                    Vec2 point);
[[nodiscard]] Vec2 inverseTransformPoint2D(const WorldTransform2D &transform,
                                           Vec2 point);
[[nodiscard]] std::optional<WorldTransform2D>
resolveWorldTransform2D(const World &world, const Entity &entity);

// Operation-scoped values in authored entity order. Overrides anchor native
// body poses while descendants without bodies still compose their local poses.
[[nodiscard]] std::vector<std::optional<WorldTransform2D>>
resolveWorldTransforms2D(
    const World &world,
    std::span<const std::optional<WorldTransform2D>> worldOverrides = {});

[[nodiscard]] Transform2DComponent
localTransform2D(const Transform2DComponent &authored,
                 const WorldTransform2D &pose,
                 const std::optional<WorldTransform2D> &parentPose);
} // namespace demi::runtime
