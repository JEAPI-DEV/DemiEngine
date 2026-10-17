#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <nlohmann/json_fwd.hpp>

namespace demi::runtime {

// Body appearance is independent of hydrology and render tessellation.
struct TerrainWaterAppearance {
  Color shallowColor{0.18F, 0.48F, 0.55F, 0.15F};
  Color deepColor{0.02F, 0.12F, 0.24F, 0.9F};
  float absorptionDistance = 3.F;
  float roughness = 0.12F;
  // Surface detail is authored in terrain-local metres and seconds. Shading
  // animation does not displace the gameplay surface or its query normal.
  float waveAmplitude = 0.05F;
  float waveLength = 2.F;
  float waveSpeed = 0.5F;
  float normalStrength = 1.F;
  float foamWidth = 0.5F;
  float foamStrength = 0.5F;
  Color foamColor{1.F, 1.F, 1.F, 1.F};
};

void validateTerrainWaterAppearance(const TerrainWaterAppearance &appearance);
TerrainWaterAppearance parseTerrainWaterAppearance(const nlohmann::json &value);
nlohmann::json
terrainWaterAppearanceJson(const TerrainWaterAppearance &appearance);
Color terrainWaterDepthColor(const TerrainWaterAppearance &appearance,
                             float depth);
bool sameTerrainWaterAppearance(const TerrainWaterAppearance &a,
                                const TerrainWaterAppearance &b);
} // namespace demi::runtime
