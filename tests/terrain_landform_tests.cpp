#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLandform.h"
#include "demi/runtime/terrain/TerrainSeed.h"

#include <FastNoiseLite.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using namespace demi::runtime;

namespace {

TerrainRecipe recipe() {
  TerrainRecipe result;
  result.size = {2000, 2000};
  result.cellsX = result.cellsZ = 64;
  result.chunkCells = 16;
  result.seed = 1704;
  auto &shape = result.landforms.at("default");
  shape.baseHeight = 250;
  shape.heightVariation = 230;
  shape.featureSize = 420;
  shape.roughness = 0.34F;
  shape.octaves = 5;
  return result;
}

void defaultsPreserveCanonicalJsonAndOriginalFbmSamples() {
  const auto source = recipe();
  const auto &shape = source.landforms.at("default");
  assert(shape.profile == "fbm" && shape.warpAmplitude == 0 &&
         shape.stretch == 1);
  const nlohmann::json original{{"base_height", shape.baseHeight},
                                {"height_variation", shape.heightVariation},
                                {"feature_size", shape.featureSize},
                                {"roughness", shape.roughness},
                                {"octaves", shape.octaves}};
  assert(shape.toJson() == original);
  assert(TerrainLandform::parse(original) == shape);
  FastNoiseLite previous(
      deriveTerrainSubSeed(source.seed, TerrainSeedChannel::Landform));
  previous.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
  previous.SetFrequency(1.0F / shape.featureSize);
  previous.SetFractalType(FastNoiseLite::FractalType_FBm);
  previous.SetFractalOctaves(shape.octaves);
  previous.SetFractalGain(shape.roughness);
  TerrainEvaluation evaluation(source);
  for (int z = 0; z <= 20; ++z) {
    for (int x = 0; x <= 20; ++x) {
      const Vec2 position{x * 91.25F, z * 87.5F};
      const float expected =
          float(double(shape.baseHeight) +
                double(shape.heightVariation) *
                    previous.GetNoise(position.x, position.y));
      assert(evaluation.baseSample(position).height == expected);
    }
  }
}

void newControlsRoundTripAndInvalidateGenerationIdentity() {
  auto source = recipe();
  const auto before = source;
  auto &shape = source.landforms.at("default");
  shape.profile = "ridged";
  shape.warpAmplitude = 80;
  shape.warpFeatureSize = 700;
  shape.stretch = 3;
  shape.heading = 80;
  const auto serialized = source.toJson();
  const auto parsed = TerrainRecipe::parse(serialized);
  assert(parsed.landforms.at("default") == shape);
  assert(parsed.toJson() == serialized);
  assert(!source.sameGenerationInputs(before));
  assert(terrainGenerationCacheKey(serialized) !=
         terrainGenerationCacheKey(before.toJson()));
  const auto assertGenerationChanged =
      [&](const TerrainLandform &changedShape) {
        auto changed = before;
        changed.landforms.at("default") = changedShape;
        assert(!changed.sameGenerationInputs(before));
      };
  const auto originalShape = before.landforms.at("default");
  auto changedShape = originalShape;
  changedShape.profile = "ridged";
  assertGenerationChanged(changedShape);
  changedShape = originalShape;
  changedShape.warpAmplitude = 1;
  assertGenerationChanged(changedShape);
  changedShape = originalShape;
  changedShape.warpFeatureSize = 256;
  assertGenerationChanged(changedShape);
  changedShape = originalShape;
  changedShape.stretch = 2;
  assertGenerationChanged(changedShape);
  changedShape = originalShape;
  changedShape.heading = 45;
  assertGenerationChanged(changedShape);
}

void directionalSamplingElongatesFeaturesAlongHeading() {
  auto normal = recipe();
  normal.landforms.at("default").profile = "ridged";
  auto stretched = normal;
  stretched.landforms.at("default").stretch = 4;
  TerrainEvaluation reference(normal);
  TerrainEvaluation elongated(stretched);
  auto rotated = stretched;
  rotated.landforms.at("default").heading = 90;
  TerrainEvaluation northward(rotated);
  auto periodic = rotated;
  periodic.landforms.at("default").heading = 450;
  TerrainEvaluation fullTurn(periodic);
  for (int z = 0; z <= 16; ++z) {
    for (int x = 0; x <= 16; ++x) {
      const Vec2 position{x * 100.0F, z * 100.0F};
      assert(elongated.baseSample(position).height ==
             reference.baseSample({position.x / 4, position.y}).height);
      assert(
          std::abs(northward.baseSample(position).height -
                   reference.baseSample({position.y / 4, -position.x}).height) <
          1e-3F);
      assert(northward.baseSample(position).height ==
             fullTurn.baseSample(position).height);
    }
  }
}

void ridgedProfileFormsCrestsAtNoiseZeroCrossings() {
  auto source = recipe();
  source.landforms.at("default").octaves = 1;
  auto ridged = source;
  ridged.landforms.at("default").profile = "ridged";
  const auto &shape = source.landforms.at("default");
  TerrainEvaluation rolling(source);
  TerrainEvaluation crests(ridged);
  for (int z = 0; z <= 16; ++z) {
    for (int x = 0; x <= 16; ++x) {
      const Vec2 point{x * 93.0F, z * 107.0F};
      const double signal =
          (double(rolling.baseSample(point).height) - shape.baseHeight) /
          shape.heightVariation;
      const double expected =
          shape.baseHeight +
          shape.heightVariation * (1.0 - 2.0 * std::abs(signal));
      assert(std::abs(crests.baseSample(point).height - expected) < 1e-3);
    }
  }
}

void warpedRangesAreDeterministicAndSeeded() {
  auto source = recipe();
  auto &shape = source.landforms.at("default");
  shape.profile = "ridged";
  shape.warpAmplitude = 80;
  shape.warpFeatureSize = 700;
  shape.stretch = 3;
  shape.heading = 80;
  const auto first = TerrainGenerator::generateBase(source);
  const auto repeated = TerrainGenerator::generateBase(source);
  assert(first && repeated && first->heights == repeated->heights);
  auto unwarped = source;
  unwarped.landforms.at("default").warpAmplitude = 0;
  const auto straight = TerrainGenerator::generateBase(unwarped);
  assert(straight && first->heights != straight->heights);
  auto differentSeed = source;
  differentSeed.seed += 1;
  const auto changed = TerrainGenerator::generateBase(differentSeed);
  assert(changed && first->heights != changed->heights);
  for (const float height : first->heights) {
    assert(std::isfinite(height));
    assert(height >= shape.baseHeight - shape.heightVariation - 1e-3F);
    assert(height <= shape.baseHeight + shape.heightVariation + 1e-3F);
  }
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::Landform) == 239678297);
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::LandformWarp) !=
         deriveTerrainSubSeed(1337, TerrainSeedChannel::Landform));
}

void invalidProfilesAndUnrepresentableSamplingAreRejected() {
  const auto rejects = [](const nlohmann::json &value) {
    bool rejected = false;
    try {
      auto shape = TerrainLandform::parse(value);
      shape.validate({2000, 2000});
    } catch (const std::exception &) {
      rejected = true;
    }
    assert(rejected);
  };
  for (const auto &value :
       {nlohmann::json{{"profile", "unknown"}},
        nlohmann::json{{"warp_amplitude", -1}},
        nlohmann::json{{"warp_feature_size", 0}},
        nlohmann::json{{"stretch", 0}},
        nlohmann::json{{"heading", std::numeric_limits<double>::infinity()}},
        nlohmann::json{{"octaves", 2.5}}, nlohmann::json{{"octaves", 10000}},
        nlohmann::json{{"feature_size", 1e-30}},
        nlohmann::json{{"warp_amplitude", 80}, {"warp_feature_size", 1e-30}},
        nlohmann::json{{"stretch", 1e-30}},
        nlohmann::json{{"unexpected", 1}}}) {
    rejects(value);
  }
}

void validationNamesTheInvalidAuthoringField() {
  const nlohmann::json invalidControls{
      {"base_height", std::numeric_limits<double>::infinity()},
      {"height_variation", -1},
      {"feature_size", 0},
      {"roughness", 2},
      {"octaves", 0},
      {"warp_amplitude", -1},
      {"warp_feature_size", 0},
      {"stretch", 0},
      {"heading", std::numeric_limits<double>::quiet_NaN()}};
  for (const auto &invalid : invalidControls.items()) {
    bool identified = false;
    try {
      (void)TerrainLandform::parse({{invalid.key(), invalid.value()}});
    } catch (const std::invalid_argument &failure) {
      identified = std::string_view(failure.what()).find(invalid.key()) !=
                   std::string_view::npos;
    }
    assert(identified);
  }
}

} // namespace

int main() {
  defaultsPreserveCanonicalJsonAndOriginalFbmSamples();
  newControlsRoundTripAndInvalidateGenerationIdentity();
  directionalSamplingElongatesFeaturesAlongHeading();
  ridgedProfileFormsCrestsAtNoiseZeroCrossings();
  warpedRangesAreDeterministicAndSeeded();
  invalidProfilesAndUnrepresentableSamplingAreRejected();
  validationNamesTheInvalidAuthoringField();
  std::cout << "Terrain landform checks passed\n";
}
