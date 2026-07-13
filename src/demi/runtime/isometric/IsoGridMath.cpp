#include "demi/runtime/isometric/IsoGridMath.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::isometric {

Vec2 tileToWorld(const GridDefinition &grid, const GridCell cell,
                 const float elevation) {
  const Vec2 fractional{.x = static_cast<float>(cell.x),
                        .y = static_cast<float>(cell.y)};
  return tileToWorld(grid, fractional, elevation);
}

Vec2 tileToWorld(const GridDefinition &grid, const Vec2 tile,
                 const float elevation) {
  const float center =
      (static_cast<float>(grid.width) + static_cast<float>(grid.height)) * 0.5F;
  return {.x = (tile.x - tile.y) * std::max(grid.cellWidth, 0.001F),
          .y = (center - tile.x - tile.y) * std::max(grid.cellHeight, 0.001F) +
               elevation};
}

Vec2 worldToTileFractional(const GridDefinition &grid, const Vec2 world) {
  const float cellWidth = std::max(grid.cellWidth, 0.001F);
  const float cellHeight = std::max(grid.cellHeight, 0.001F);
  const float difference = world.x / cellWidth;
  const float sum =
      (static_cast<float>(grid.width) + static_cast<float>(grid.height)) *
          0.5F -
      world.y / cellHeight;
  return {.x = (sum + difference) * 0.5F, .y = (sum - difference) * 0.5F};
}

GridCell worldToTile(const GridDefinition &grid, const Vec2 world) {
  const Vec2 tile = worldToTileFractional(grid, world);
  return {.x = static_cast<int>(std::floor(tile.x + 0.5F)),
          .y = static_cast<int>(std::floor(tile.y + 0.5F))};
}

bool contains(const GridDefinition &grid, const GridCell cell) {
  return cell.x >= 0 && cell.y >= 0 && cell.x < grid.width &&
         cell.y < grid.height;
}

} // namespace demi::runtime::isometric
