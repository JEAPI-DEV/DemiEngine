#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime::terrain_detail {

// Shared full/reduced terrain convention: local world distance times biome
// scale.
Vec2 terrainUv(Vec2 position, float textureScale);

} // namespace demi::runtime::terrain_detail
