#include "demi/runtime/math/VectorMath.h"

#include <algorithm>

namespace demi::runtime::math {

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
