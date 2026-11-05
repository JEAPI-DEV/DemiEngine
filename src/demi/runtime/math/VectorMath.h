#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cmath>

namespace demi::runtime::math {

[[nodiscard]] constexpr Vec2 add(const Vec2 left, const Vec2 right) {
  return {left.x + right.x, left.y + right.y};
}

[[nodiscard]] constexpr Vec3 add(const Vec3 left, const Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr Vec2 subtract(const Vec2 left, const Vec2 right) {
  return {left.x - right.x, left.y - right.y};
}

[[nodiscard]] constexpr Vec3 subtract(const Vec3 left, const Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr Vec2 scale(const Vec2 value, const float amount) {
  return {value.x * amount, value.y * amount};
}

[[nodiscard]] constexpr Vec3 scale(const Vec3 value, const float amount) {
  return {value.x * amount, value.y * amount, value.z * amount};
}

[[nodiscard]] constexpr float dot(const Vec2 left, const Vec2 right) {
  return left.x * right.x + left.y * right.y;
}

[[nodiscard]] constexpr float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] inline float length(const Vec2 value) {
  return std::sqrt(dot(value, value));
}
[[nodiscard]] inline float length(const Vec3 value) {
  return std::sqrt(dot(value, value));
}

[[nodiscard]] inline Vec2 normalized(const Vec2 value) {
  const float magnitude = length(value);
  return magnitude > 0.000001F ? scale(value, 1.0F / magnitude) : Vec2{};
}

[[nodiscard]] inline Vec3 normalized(const Vec3 value) {
  const float magnitude = length(value);
  return magnitude > 0.000001F ? scale(value, 1.0F / magnitude) : Vec3{};
}

[[nodiscard]] constexpr Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] Vec2 lerp(Vec2 from, Vec2 to, float amount);
[[nodiscard]] Vec3 lerp(Vec3 from, Vec3 to, float amount);
[[nodiscard]] float smoothstep(float minimum, float maximum, float value);

} // namespace demi::runtime::math
