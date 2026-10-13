#include "demi/runtime/terrain/TerrainLandform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::invalid_argument(message);
  }
}

void validateControls(const TerrainLandform &shape) {
  require(std::ranges::find(TerrainLandform::profileNames, shape.profile) !=
              TerrainLandform::profileNames.end(),
          "Terrain landform profile must be fbm or ridged");
  require(std::isfinite(shape.baseHeight),
          "Terrain landform base_height must be finite");
  require(std::isfinite(shape.heightVariation) && shape.heightVariation >= 0,
          "Terrain landform height_variation must be finite and nonnegative");
  require(std::isfinite(shape.featureSize) && shape.featureSize > 0,
          "Terrain landform feature_size must be finite and positive");
  require(std::isfinite(shape.roughness) && shape.roughness >= 0 &&
              shape.roughness <= 1,
          "Terrain landform roughness must be within [0,1]");
  require(shape.octaves > 0, "Terrain landform octaves must be positive");
  require(std::isfinite(shape.warpAmplitude) && shape.warpAmplitude >= 0,
          "Terrain landform warp_amplitude must be finite and nonnegative");
  require(std::isfinite(shape.warpFeatureSize) && shape.warpFeatureSize > 0,
          "Terrain landform warp_feature_size must be finite and positive");
  require(std::isfinite(shape.stretch) && shape.stretch > 0,
          "Terrain landform stretch must be finite and positive");
  require(std::isfinite(shape.heading),
          "Terrain landform heading must be finite");
  require(double(std::abs(shape.baseHeight)) + shape.heightVariation <=
              std::numeric_limits<float>::max(),
          "Terrain landform base_height/height_variation range overflows");
  require(std::isfinite(1.0F / shape.featureSize),
          "Terrain feature_size produces an unrepresentable noise frequency");
  if (shape.warpAmplitude > 0) {
    require(std::isfinite(1.0F / shape.warpFeatureSize),
            "Terrain warp_feature_size produces an unrepresentable frequency");
  }
}

} // namespace

TerrainLandform TerrainLandform::parse(const nlohmann::json &json) {
  require(json.is_object(), "Terrain landform must be an object");
  constexpr std::array<std::string_view, 10> names{
      "base_height", "height_variation", "feature_size",   "roughness",
      "octaves",     "profile",          "warp_amplitude", "warp_feature_size",
      "stretch",     "heading"};
  for (const auto &entry : json.items()) {
    if (std::ranges::find(names, entry.key()) == names.end()) {
      throw std::invalid_argument("Unknown terrain landform field: " +
                                  entry.key());
    }
  }
  TerrainLandform shape;
  shape.baseHeight = json.value("base_height", shape.baseHeight);
  shape.heightVariation = json.value("height_variation", shape.heightVariation);
  shape.featureSize = json.value("feature_size", shape.featureSize);
  shape.roughness = json.value("roughness", shape.roughness);
  if (const auto value = json.find("octaves"); value != json.end()) {
    require(value->is_number_integer(),
            "Terrain landform octaves must be an integer");
    const auto number = value->get<long double>();
    require(number > 0 && number <= std::numeric_limits<int>::max(),
            "Terrain landform octaves is outside the positive integer range");
    shape.octaves = static_cast<int>(number);
  }
  shape.profile = json.value("profile", shape.profile);
  shape.warpAmplitude = json.value("warp_amplitude", shape.warpAmplitude);
  shape.warpFeatureSize =
      json.value("warp_feature_size", shape.warpFeatureSize);
  shape.stretch = json.value("stretch", shape.stretch);
  shape.heading = json.value("heading", shape.heading);
  validateControls(shape);
  return shape;
}

nlohmann::json TerrainLandform::toJson() const {
  nlohmann::json result{{"base_height", baseHeight},
                        {"height_variation", heightVariation},
                        {"feature_size", featureSize},
                        {"roughness", roughness},
                        {"octaves", octaves}};
  // Preserve the canonical representation of existing FBm recipes. New source
  // need only specify controls that differ from this shape's native defaults.
  if (profile != "fbm") {
    result["profile"] = profile;
  }
  if (warpAmplitude != 0) {
    result["warp_amplitude"] = warpAmplitude;
  }
  if (warpFeatureSize != 128) {
    result["warp_feature_size"] = warpFeatureSize;
  }
  if (stretch != 1) {
    result["stretch"] = stretch;
  }
  if (heading != 0) {
    result["heading"] = heading;
  }
  return result;
}

void TerrainLandform::validate(Vec2 extent) const {
  validateControls(*this);
  require(std::isfinite(extent.x) && std::isfinite(extent.y) && extent.x > 0 &&
              extent.y > 0,
          "Terrain landform requires a positive finite extent");
  // BasicGrid warping interpolates unit vector components. Its normalized
  // three-octave amplitude sum therefore bounds each displacement component.
  const double warpedX = double(extent.x) + warpAmplitude;
  const double warpedZ = double(extent.y) + warpAmplitude;
  const double headingDegrees = std::remainder(double(heading), 360.0);
  const double radius = headingDegrees == 0 ? std::max(warpedX, warpedZ)
                                            : std::hypot(warpedX, warpedZ);
  const double coordinateBound = radius * std::max(1.0, 1.0 / double(stretch));
  require(std::isfinite(coordinateBound) &&
              coordinateBound <= std::numeric_limits<float>::max(),
          "Terrain landform sampling coordinates overflow");
  const double frequency = std::ldexp(1.0 / featureSize, octaves - 1);
  const double latticeLimit = std::numeric_limits<int>::max() / 4.0;
  require(std::isfinite(frequency) &&
              frequency * coordinateBound < latticeLimit,
          "Terrain octave coordinates exceed noise lattice range");
  if (warpAmplitude > 0) {
    const double warpFrequency =
        std::ldexp(1.0 / warpFeatureSize, warpOctaves - 1);
    require(std::isfinite(warpFrequency) &&
                warpFrequency * std::max(warpedX, warpedZ) < latticeLimit,
            "Terrain warp coordinates exceed noise lattice range");
  }
}

} // namespace demi::runtime
