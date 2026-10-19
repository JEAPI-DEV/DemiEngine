#pragma once

#include "demi/runtime/terrain/TerrainWater.h"
#include <cstddef>
#include <optional>
#include <string_view>

namespace demi::runtime {
struct TerrainDrainage;
struct HeightField;

struct TerrainWaterProposalSettings {
  float minimumDepth = 1.F;
  float minimumArea = 16.F;
  std::size_t maximumBodies = 16;
  // Lower a proposed surface below its actual priority-flood spill level.
  float levelOffset = 0.F;
};
struct TerrainWaterProposals {
  TerrainWaterAuthoring water;
  std::size_t eligibleBodies = 0;
  bool truncated = false;
};

// No carving or fluid/spill simulation. Closed priority-flood basins propose
// connected lakes at or below their measured spill level. IDs use the owning
// node ID and lowest-ground sink coordinates, never output-list positions.
[[nodiscard]] std::optional<TerrainWaterProposals> proposeTerrainBasinWater(
    const HeightField &field, const TerrainDrainage &drainage,
    std::string_view ownerId, const TerrainWaterProposalSettings &settings = {},
    std::stop_token stop = {});
} // namespace demi::runtime
