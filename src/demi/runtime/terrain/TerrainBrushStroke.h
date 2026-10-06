#pragma once

#include <nlohmann/json.hpp>

namespace demi::runtime {

// Append one center-based radial stamp, folding only an adjacent stamp with
// exactly the same authored parameters. Snapshot-bearing stamps stay separate.
void appendTerrainBrushStamp(nlohmann::json &entries, nlohmann::json stamp);

// Preserve stamp order while folding adjacent equal parameters into points.
nlohmann::json compactTerrainBrushEntries(nlohmann::json entries);

// Compact only authored stroke arrays; preserve every other recipe field.
nlohmann::json compactTerrainBrushRecipe(nlohmann::json recipe);

// Expand points into ordered center-based entries for native recipe parsing.
nlohmann::json expandTerrainBrushEntries(const nlohmann::json &entries);

} // namespace demi::runtime
