#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime::terrain_graph_detail {
inline float checkedTerrainGraphHeight(double value) {
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted)) {
    throw std::invalid_argument("Terrain graph produced a nonfinite height");
  }
  return converted;
}

inline float terrainGraphBandWeight(float value, float minimum, float maximum,
                                    float falloff) {
  if (minimum > maximum || falloff < 0) {
    throw std::invalid_argument(
        "Mask band requires minimum <= maximum and nonnegative falloff");
  }
  if (value >= minimum && value <= maximum) {
    return 1;
  }
  if (falloff == 0) {
    return 0;
  }
  return std::clamp(value < minimum ? (value - minimum + falloff) / falloff
                                    : (maximum + falloff - value) / falloff,
                    0.0F, 1.0F);
}

inline float terrainGraphModifiedHeight(float first, float second,
                                        double amount, float mask, bool blend) {
  const double weight = amount * mask;
  return checkedTerrainGraphHeight(
      blend ? first * (1 - weight) + second * weight : first + weight);
}
} // namespace demi::runtime::terrain_graph_detail
