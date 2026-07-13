#include "demi/runtime/isometric/GridOccupancy.h"

#include "demi/runtime/isometric/IsoGridMath.h"

#include <algorithm>

namespace demi::runtime::isometric {

GridOccupancy::GridOccupancy(GridDefinition definition)
    : definition_(std::move(definition)) {}

const GridDefinition &GridOccupancy::definition() const { return definition_; }

std::vector<GridCell>
GridOccupancy::cellsFor(const GridCell origin,
                        const GridFootprint footprint) const {
  std::vector<GridCell> cells;
  const int width = std::max(footprint.width, 1);
  const int height = std::max(footprint.height, 1);
  cells.reserve(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      cells.push_back({.x = origin.x + x, .y = origin.y + y});
  return cells;
}

bool GridOccupancy::isFree(const GridCell origin, const GridFootprint footprint,
                           const std::string &ignoredEntityId) const {
  for (const GridCell cell : cellsFor(origin, footprint)) {
    if (!contains(definition_, cell))
      return false;
    const auto found = occupants_.find(cell);
    if (found != occupants_.end() && found->second != ignoredEntityId)
      return false;
  }
  return true;
}

bool GridOccupancy::occupy(const GridCell origin, const GridFootprint footprint,
                           const std::string &entityId) {
  if (entityId.empty() || !isFree(origin, footprint, entityId))
    return false;
  release(entityId);
  for (const GridCell cell : cellsFor(origin, footprint))
    occupants_[cell] = entityId;
  return true;
}

void GridOccupancy::release(const std::string &entityId) {
  std::erase_if(occupants_,
                [&](const auto &entry) { return entry.second == entityId; });
}

std::optional<std::string> GridOccupancy::occupant(const GridCell cell) const {
  const auto found = occupants_.find(cell);
  return found == occupants_.end() ? std::nullopt
                                   : std::optional<std::string>{found->second};
}

} // namespace demi::runtime::isometric
