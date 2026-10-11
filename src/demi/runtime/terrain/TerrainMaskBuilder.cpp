#include "demi/runtime/terrain/TerrainMaskBuilder.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"
#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include "demi/runtime/terrain/TerrainWater.h"

#include <algorithm>
#include <vector>
#include <stdexcept>

namespace demi::runtime {
// The rule context supplies slope and moisture. Final drainage supplies water
// distance and flow after processing has changed the surface.
bool buildTerrainMasks(const HeightField &field, const TerrainRecipe &recipe,
                       const TerrainDrainage *drainage,
                       const TerrainSamples<float> *sediment, TerrainMasks &out,
                       bool includeFlow, std::stop_token stop) {
  const auto count = field.heights.size();
  if (count == 0) {
    return false;
  }
  std::vector<float> heights(count);
  for (std::size_t index = 0; index < count; ++index) {
    if ((index & 255) == 0 && stop.stop_requested()) {
      return false;
    }
    heights[index] = field.heights[index];
  }

  if (stop.stop_requested()) {
    return false;
  }
  TerrainRuleContextBuilder contexts(recipe);
  contexts.build(recipe, heights, field.cellsX, field.cellsZ, field.size);
  if (stop.stop_requested()) {
    return false;
  }
  const auto &derived = contexts.contexts();
  if (derived.size() != count) {
    return false;
  }

  out.slope.resize(count);
  out.moisture.resize(count);
  out.waterDistance.resize(count);
  out.flow.resize(count);
  out.sediment.resize(count);
  out.substrate.resize(count);
  const float shoreBand =
      std::max(field.size.x / field.cellsX, field.size.y / field.cellsZ) * 2.F;
  const bool useAuthoredSeaLevel =
      drainage != nullptr &&
      drainage->seaLevel != TerrainRuleContextBuilder::seaLevel(recipe);
  for (std::size_t index = 0; index < count; ++index) {
    if ((index & 255) == 0 && stop.stop_requested()) {
      return false;
    }
    out.slope.set(index, derived[index].slope);
    out.moisture.set(index, derived[index].moisture);
    const float waterDistance = drainage != nullptr
                                    ? drainage->waterDistance[index]
                                    : derived[index].waterDistance;
    out.waterDistance.set(index, waterDistance);
    // Rule contexts use the recipe's inferred sea level. When the caller has
    // authored another level, classify the final substrate against that level.
    auto substrate = derived[index].substrate;
    if (useAuthoredSeaLevel) {
      if (heights[index] <= drainage->seaLevel) {
        substrate = TerrainSubstrate::Wet;
      } else if (derived[index].slope >= 38.F) {
        substrate = TerrainSubstrate::Rock;
      } else if (waterDistance <= shoreBand) {
        substrate = TerrainSubstrate::Sand;
      } else {
        substrate = TerrainSubstrate::Soil;
      }
    }
    out.substrate.set(index, static_cast<std::size_t>(substrate));
    const bool hasFlowSample =
        includeFlow && drainage != nullptr && index < drainage->flow.size();
    const float flow = hasFlowSample ? drainage->flow[index] : 0.F;
    out.flow.set(index, flow);
    out.sediment.set(index, sediment != nullptr ? (*sediment)[index] : 0.F);
  }
  return true;
}

bool applyTerrainWaterMasks(
    const HeightField &field,
    const terrain_water_detail::WaterLevelField &coverage, TerrainMasks &masks,
    std::stop_token stop) {
  const auto sampleCount = field.heights.size();
  const bool matchingGrid = coverage.cellsX == field.cellsX &&
                            coverage.cellsZ == field.cellsZ &&
                            coverage.size.x == field.size.x &&
                            coverage.size.y == field.size.y;
  if (!matchingGrid || coverage.level.size() != sampleCount ||
      coverage.wet.size() != sampleCount || masks.count() != sampleCount) {
    throw std::invalid_argument("Water coverage does not match material masks");
  }

  const auto distances = computeTerrainWaterDistances(field, coverage.wet, stop);
  if (stop.stop_requested()) {
    return false;
  }

  const float shoreBand =
      std::max(field.size.x / field.cellsX, field.size.y / field.cellsZ) * 2.F;
  masks.waterDepth.resize(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return false;
    }
    const bool submerged = coverage.wet[sample] != 0;
    const float depth = submerged
                            ? coverage.level[sample] - field.heights[sample]
                            : -1.F;
    masks.waterDepth.set(sample, depth);
    masks.waterDistance.set(sample, distances[sample]);

    TerrainSubstrate substrate = TerrainSubstrate::Soil;
    if (submerged) {
      substrate = TerrainSubstrate::Wet;
    } else if (masks.slope[sample] >= 38.F) {
      substrate = TerrainSubstrate::Rock;
    } else if (distances[sample] <= shoreBand) {
      substrate = TerrainSubstrate::Sand;
    }
    masks.substrate.set(sample, static_cast<std::size_t>(substrate));
  }
  return true;
}

} // namespace demi::runtime
