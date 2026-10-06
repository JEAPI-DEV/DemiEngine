#pragma once

#include "demi/runtime/terrain/TerrainGeneration.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include <functional>
#include <optional>
#include <stop_token>

namespace demi::runtime {

using TerrainProgress = std::function<void(float)>;

// Which optional stages to run. The landform, surface and sculpt stages are
// always run; these are the expensive or situation-dependent ones, so a caller
// can ask for a shape-only result without paying for erosion.
struct TerrainPipelineSettings {
  TerrainQuality quality = TerrainQuality::Standard;
  // For graph recipes, controls final derived flow masks, not graph nodes.
  bool drainage = true;
  // Explicit erosion and preview overrides conflict with graph recipes.
  bool erosion = false;
  // Supplies the palette to scatter. Generation never loads assets, so the
  // caller passes the resolved palette it already holds.
  const TerrainPalette *palette = nullptr;
  TerrainWaterLevel water{};
  // River, lake and ocean bodies. Null means no water authoring, which is not
  // the same as authoring water and having every body rejected. Authored water
  // here conflicts with graph water nodes.
  const struct TerrainWaterAuthoring *waterAuthoring = nullptr;
  // Where a rejected water body is reported. Optional, because a caller that
  // does not surface diagnostics should not be forced to receive them.
  std::string *waterError = nullptr;
};

// Runs the generation pipeline in the declared stage order and reports what
// actually ran. A graph recipe executes its own stages; this entry derives
// final masks from that output without replaying graph or manual stages.
//
// The point of the result type is that a caller is never left guessing. A
// result records optional stages; a caller can ask whether a stage ran
// instead of reading a fabricated zero out of a mask that was never computed.
[[nodiscard]] std::optional<TerrainGenerationResult>
generateTerrainStages(const TerrainRecipe &recipe,
                      const TerrainPipelineSettings &settings = {},
                      std::stop_token stop = {},
                      const TerrainProgress &progress = {});

// A single line naming the quality, the stages that ran and whether masks are
// present. Used by the editor status line and by the CLI, so "what did you
// actually generate" has one answer everywhere.
[[nodiscard]] std::string describeTerrainStages(const TerrainGenerationResult &);

} // namespace demi::runtime
