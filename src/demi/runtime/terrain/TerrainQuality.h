#pragma once

#include <string_view>

namespace demi::runtime {

// Transient generation work policy. Both tiers retain the authored grid and
// source shape; Preview only reduces erosion work.
enum class TerrainQuality { Preview, Standard };

[[nodiscard]] std::string_view terrainQualityName(TerrainQuality quality);

} // namespace demi::runtime
