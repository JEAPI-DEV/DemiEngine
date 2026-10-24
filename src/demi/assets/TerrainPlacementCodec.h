#pragma once

#include "demi/runtime/terrain/TerrainHeightField.h"

#include <cstddef>
#include <span>
#include <vector>

namespace demi::assets::terrain_payload {

// One shared definition dictionary covers final placements and graph
// candidates. Instance records preserve exact float values and durable
// candidate coordinates.
std::vector<std::byte> encodePlacements(const runtime::HeightField &field);
void decodePlacements(std::span<const std::byte> bytes,
                      runtime::HeightField &field);

} // namespace demi::assets::terrain_payload
