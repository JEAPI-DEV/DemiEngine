#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

namespace demi::runtime::terrain_water_detail {
// Selects the nearest wet sample to the authored centre, then its connected
// component using the terrain's six triangle edges. Zero-depth land cannot
// bridge components. The caller builds the wet mask once from body coverage.
std::vector<std::uint8_t>
connectedLakeCoverage(Vec2 size, int cellsX, int cellsZ, Vec2 centre,
                      std::span<const std::uint8_t> wet,
                      std::stop_token stop = {});
} // namespace demi::runtime::terrain_water_detail
