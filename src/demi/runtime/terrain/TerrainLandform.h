#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace demi::runtime {

// Elevation controls are independent of biome appearance. A directional shape
// stretches feature length along heading, measured from +X toward +Z.
struct TerrainLandform {
  static constexpr std::array<std::string_view, 2> profileNames{"fbm",
                                                                "ridged"};
  static constexpr int warpOctaves = 3;

  float baseHeight = 0;
  float heightVariation = 8;
  float featureSize = 32;
  float roughness = .5F;
  int octaves = 4;
  std::string profile = "fbm";
  float warpAmplitude = 0;
  float warpFeatureSize = 128;
  float stretch = 1;
  float heading = 0;

  static TerrainLandform parse(const nlohmann::json &json);
  nlohmann::json toJson() const;
  // Includes noise lattice and coordinate representability for this extent.
  void validate(Vec2 extent) const;
  bool operator==(const TerrainLandform &) const = default;
};

} // namespace demi::runtime
