#include "demi/runtime/terrain/TerrainWaterAppearance.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
namespace {
void validateColor(Color color) {
  for (const float channel : {color.r, color.g, color.b, color.a})
    if (!std::isfinite(channel) || channel < 0 || channel > 1)
      throw std::invalid_argument(
          "Water colour requires normalized finite RGBA channels");
}
Color readColor(const nlohmann::json &value) {
  if (!value.is_array() || value.size() != 4)
    throw std::invalid_argument("Water colour requires four RGBA channels");
  Color color{value.at(0).get<float>(), value.at(1).get<float>(),
              value.at(2).get<float>(), value.at(3).get<float>()};
  validateColor(color);
  return color;
}
} // namespace

void validateTerrainWaterAppearance(const TerrainWaterAppearance &appearance) {
  validateColor(appearance.shallowColor);
  validateColor(appearance.deepColor);
  if (!std::isfinite(appearance.absorptionDistance) ||
      appearance.absorptionDistance <= 0)
    throw std::invalid_argument(
        "Water absorption distance must be finite and positive");
  if (!std::isfinite(appearance.roughness) || appearance.roughness < 0 ||
      appearance.roughness > 1)
    throw std::invalid_argument(
        "Water roughness must be finite and between zero and one");
}

TerrainWaterAppearance
parseTerrainWaterAppearance(const nlohmann::json &value) {
  if (!value.is_object())
    throw std::invalid_argument("Water appearance must be an object");
  TerrainWaterAppearance appearance;
  if (value.contains("shallow_color"))
    appearance.shallowColor = readColor(value.at("shallow_color"));
  if (value.contains("deep_color"))
    appearance.deepColor = readColor(value.at("deep_color"));
  appearance.absorptionDistance =
      value.value("absorption_distance", appearance.absorptionDistance);
  appearance.roughness = value.value("roughness", appearance.roughness);
  validateTerrainWaterAppearance(appearance);
  return appearance;
}

nlohmann::json
terrainWaterAppearanceJson(const TerrainWaterAppearance &appearance) {
  validateTerrainWaterAppearance(appearance);
  const auto color = [](Color value) {
    return nlohmann::json::array({value.r, value.g, value.b, value.a});
  };
  return {{"shallow_color", color(appearance.shallowColor)},
          {"deep_color", color(appearance.deepColor)},
          {"absorption_distance", appearance.absorptionDistance},
          {"roughness", appearance.roughness}};
}

Color terrainWaterDepthColor(const TerrainWaterAppearance &appearance,
                             float depth) {
  const float shallowWeight =
      std::exp(-std::max(0.F, depth) / appearance.absorptionDistance);
  const auto blend = [&](float shallow, float deep) {
    return std::lerp(deep, shallow, shallowWeight);
  };
  return {blend(appearance.shallowColor.r, appearance.deepColor.r),
          blend(appearance.shallowColor.g, appearance.deepColor.g),
          blend(appearance.shallowColor.b, appearance.deepColor.b),
          blend(appearance.shallowColor.a, appearance.deepColor.a)};
}

bool sameTerrainWaterAppearance(const TerrainWaterAppearance &a,
                                const TerrainWaterAppearance &b) {
  const auto color = [](Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b &&
           left.a == right.a;
  };
  return color(a.shallowColor, b.shallowColor) &&
         color(a.deepColor, b.deepColor) &&
         a.absorptionDistance == b.absorptionDistance &&
         a.roughness == b.roughness;
}
} // namespace demi::runtime
