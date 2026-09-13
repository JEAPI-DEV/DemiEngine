#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime::math {

[[nodiscard]] Vec2 add(Vec2 left, Vec2 right);
[[nodiscard]] Vec3 add(Vec3 left, Vec3 right);
[[nodiscard]] Vec2 subtract(Vec2 left, Vec2 right);
[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right);
[[nodiscard]] Vec2 scale(Vec2 value, float amount);
[[nodiscard]] Vec3 scale(Vec3 value, float amount);
[[nodiscard]] float length(Vec2 value);
[[nodiscard]] float length(Vec3 value);
[[nodiscard]] Vec2 normalized(Vec2 value);
[[nodiscard]] Vec3 normalized(Vec3 value);
[[nodiscard]] float dot(Vec2 left, Vec2 right);
[[nodiscard]] float dot(Vec3 left, Vec3 right);
[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right);
[[nodiscard]] Vec2 lerp(Vec2 from, Vec2 to, float amount);
[[nodiscard]] Vec3 lerp(Vec3 from, Vec3 to, float amount);
[[nodiscard]] float smoothstep(float minimum, float maximum, float value);

} // namespace demi::runtime::math
