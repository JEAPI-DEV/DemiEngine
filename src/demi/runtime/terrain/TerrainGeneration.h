#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/runtime/terrain/TerrainWater.h"
#include "demi/runtime/terrain/TerrainMasks.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace demi::runtime {

// Generation order, as the pipeline actually runs it.
//
// Biome assignment follows the base landform. Drainage, erosion and water
// carving precede manual edits; masks describe the final surface.
enum class TerrainStage {
  Landform,   // base height from the recipe's default landform
  Drainage,   // flow accumulation, basins, distance to water
  Erosion,    // hydraulic and thermal transport over the drained surface
  Hydrology,  // river and lake channels carved from the drainage field
  Masks,      // derived per-sample inputs the rules and scattering read
  Surface,    // biome rules assign appearance
  Sculpt,     // authored strokes
};

constexpr std::array<TerrainStage, 7> terrainStageOrder{
    TerrainStage::Landform, TerrainStage::Surface, TerrainStage::Drainage,
    TerrainStage::Erosion, TerrainStage::Hydrology, TerrainStage::Sculpt,
    TerrainStage::Masks};

[[nodiscard]] std::string_view terrainStageName(TerrainStage stage);

// Quality tiers. Preview exists so an author can iterate on shape without paying
// for the expensive stages; it is never a silent downgrade, because the field
// records which tier produced it.
enum class TerrainQuality {
  Preview,  // fewer octaves, no erosion, coarser drainage
  Standard, // full stages at the recipe's authored parameters
};

[[nodiscard]] std::string_view terrainQualityName(TerrainQuality quality);

// A world-unit height at which a sample is water. Authored rather than guessed,
// because every downstream stage that needs a shoreline needs the same number.
struct TerrainWaterLevel {
  float seaLevel = 0;
  bool authored = false;
};

// The generated result, including everything a consumer needs to interpret it.
// Populated by the generator; consumers read it and never re-derive.
struct TerrainGenerationResult {
  std::shared_ptr<const HeightField> field;
  TerrainMasks masks;
  TerrainWaterLevel water;
  // Retains the hydrology stage output before later manual surface edits.
  std::optional<TerrainWaterResult> waterResult;
  TerrainQuality quality = TerrainQuality::Standard;
  // Which stages actually ran, in order. Optional stages are recorded only
  // when they ran.
  std::vector<TerrainStage> stages;
  // A stable digest of the parameters that produced this result, so a consumer
  // can tell a preview from a final result without comparing every field.
  std::uint64_t fingerprint = 0;
};

// True when a stage ran. Consumers must treat an absent stage as "not computed"
// rather than assuming a default, which is what a fabricated zero would invite.
[[nodiscard]] bool terrainStageRan(const TerrainGenerationResult &result,
                                  TerrainStage stage);

} // namespace demi::runtime
