#pragma once
#include "demi/runtime/render/backend/GpuResources.h"
#include <cmath>
#include <stdexcept>

namespace demi::runtime::render {
// Reduce only the constant tile origin, in double precision. Adding a large
// origin to nearby fragment coordinates first loses sub-texel movement.
inline float textureProjectionPhase3D(double origin, double tiling, TextureWrap wrap) {
  const double coordinate = origin * tiling;
  if (!std::isfinite(coordinate)) {
    throw std::invalid_argument("Texture projection origin must remain finite");
  }
  if (wrap == TextureWrap::Clamp) {
    const auto value = static_cast<float>(coordinate);
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Clamped texture projection exceeds float precision range");
    }
    return value;
  }
  const double period = wrap == TextureWrap::Mirror ? 2.0 : 1.0;
  return static_cast<float>(coordinate - std::floor(coordinate / period) * period);
}
} // namespace demi::runtime::render
