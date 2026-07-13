#pragma once

#include "demi/runtime/isometric/GridTypes.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime::isometric {

[[nodiscard]] Vec2 tileToWorld(const GridDefinition &grid, GridCell cell,
                               float elevation = 0.0F);
[[nodiscard]] Vec2 tileToWorld(const GridDefinition &grid, Vec2 tile,
                               float elevation = 0.0F);
[[nodiscard]] Vec2 worldToTileFractional(const GridDefinition &grid,
                                         Vec2 world);
[[nodiscard]] GridCell worldToTile(const GridDefinition &grid, Vec2 world);
[[nodiscard]] bool contains(const GridDefinition &grid, GridCell cell);

} // namespace demi::runtime::isometric
