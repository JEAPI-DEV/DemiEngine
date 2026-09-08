#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>

namespace demi::runtime {

struct TwoBoneIkResult2D {
  Vec2 joint;
  Vec2 end;
  bool reached = false;
};

struct TwoBoneIkResult3D {
  Vec3 joint;
  Vec3 end;
  bool reached = false;
};

// Solves a two-segment chain toward target. The pole selects the bend side or
// plane. Unreachable targets are clamped to the nearest valid chain endpoint.
[[nodiscard]] std::optional<TwoBoneIkResult2D>
solveTwoBoneIk2D(Vec2 root, Vec2 target, Vec2 pole, float upperLength,
                 float lowerLength);

[[nodiscard]] std::optional<TwoBoneIkResult3D>
solveTwoBoneIk3D(Vec3 root, Vec3 target, Vec3 pole, float upperLength,
                 float lowerLength);

} // namespace demi::runtime
