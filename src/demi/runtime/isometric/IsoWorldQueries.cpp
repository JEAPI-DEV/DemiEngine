#include "demi/runtime/isometric/IsoWorldQueries.h"

#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>

namespace demi::runtime::isometric {

std::optional<GridDefinition> gridDefinition(const World &world) {
  for (const Entity &entity : world.entities) {
    if (const auto *grid = entity.component<IsoGridComponent>())
      return GridDefinition{.width = grid->width,
                            .height = grid->height,
                            .cellWidth = grid->cellSize.x,
                            .cellHeight = grid->cellSize.y};
  }
  return std::nullopt;
}

GridOccupancy buildOccupancy(const World &world) {
  GridOccupancy occupancy(gridDefinition(world).value_or(GridDefinition{}));
  for (const Entity &entity : world.entities) {
    const auto *transform = entity.component<IsoTransformComponent>();
    const auto *buildable = entity.component<BuildableComponent>();
    if (transform == nullptr || buildable == nullptr ||
        !buildable->blocksMovement)
      continue;
    (void)occupancy.occupy(
        {.x = static_cast<int>(transform->tile.x),
         .y = static_cast<int>(transform->tile.y)},
        {.width = std::max(static_cast<int>(transform->footprint.x), 1),
         .height = std::max(static_cast<int>(transform->footprint.y), 1)},
        entity.id);
  }
  return occupancy;
}

std::optional<std::string> entityAt(const World &world, const GridCell cell) {
  for (auto iterator = world.entities.rbegin();
       iterator != world.entities.rend(); ++iterator) {
    const auto *transform = iterator->component<IsoTransformComponent>();
    if (transform == nullptr)
      continue;
    const int originX = static_cast<int>(transform->tile.x);
    const int originY = static_cast<int>(transform->tile.y);
    const int width = std::max(static_cast<int>(transform->footprint.x), 1);
    const int height = std::max(static_cast<int>(transform->footprint.y), 1);
    if (cell.x >= originX && cell.y >= originY && cell.x < originX + width &&
        cell.y < originY + height)
      return iterator->id;
  }
  return std::nullopt;
}

PlacementResult queryPlacement(const World &world, const GridCell origin,
                               const GridFootprint footprint) {
  return evaluatePlacement(buildOccupancy(world),
                           {.origin = origin, .footprint = footprint});
}

} // namespace demi::runtime::isometric
