#include "demi/runtime/math/VectorMath.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::math {

Vec2 add(const Vec2 left, const Vec2 right) {
  return {left.x + right.x, left.y + right.y};
}

Vec3 add(const Vec3 left, const Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec2 subtract(const Vec2 left, const Vec2 right) {
  return {left.x - right.x, left.y - right.y};
}

Vec3 subtract(const Vec3 left, const Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec2 scale(const Vec2 value, const float amount) {
  return {value.x * amount, value.y * amount};
}

Vec3 scale(const Vec3 value, const float amount) {
  return {value.x * amount, value.y * amount, value.z * amount};
}

float dot(const Vec2 left, const Vec2 right) {
  return left.x * right.x + left.y * right.y;
}

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

float length(const Vec2 value) { return std::sqrt(dot(value, value)); }
float length(const Vec3 value) { return std::sqrt(dot(value, value)); }

Vec2 normalized(const Vec2 value) {
  const float magnitude = length(value);
  return magnitude > 0.000001F ? scale(value, 1.0F / magnitude) : Vec2{};
}

Vec3 normalized(const Vec3 value) {
  const float magnitude = length(value);
  return magnitude > 0.000001F ? scale(value, 1.0F / magnitude) : Vec3{};
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Vec2 lerp(const Vec2 from, const Vec2 to, const float amount) {
  return add(from, scale(subtract(to, from), amount));
}

Vec3 lerp(const Vec3 from, const Vec3 to, const float amount) {
  return add(from, scale(subtract(to, from), amount));
}

float smoothstep(const float minimum, const float maximum, const float value) {
  if (minimum == maximum)
    return value < minimum ? 0.0F : 1.0F;
  const float amount =
      std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F);
  return amount * amount * (3.0F - 2.0F * amount);
}

} // namespace demi::runtime::math
