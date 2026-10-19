#pragma once

#include "demi/runtime/terrain/TerrainWater.h"
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>

namespace demi::runtime {
// Complete shared contract used by graph authoring, edits and native hydrology.
void validateTerrainWaterBody(const TerrainWaterBodySpec &body,
                              std::stop_token stop = {});
[[nodiscard]] TerrainWaterBodySpec
parseTerrainWaterBodyParameters(const nlohmann::json &parameters,
                                std::string id,
                                const TerrainWaterBodySpec &defaults = {});
// Exclusions remove published water, not an upstream channel/basin carve.
// Overrides patch stable body IDs and retain unspecified native values.
[[nodiscard]] TerrainWaterAuthoring
editTerrainWaterAuthoring(const TerrainWaterAuthoring &source,
                          std::span<const std::string> excludedIds,
                          const nlohmann::json &overrides);
void validateTerrainWaterEdits(const nlohmann::json &excludedIds,
                               const nlohmann::json &overrides);
} // namespace demi::runtime
