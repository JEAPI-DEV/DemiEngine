#include "demi/runtime/isometric/GridPathfinder.h"
#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/PlacementRules.h"

#include <cmath>
#include <iostream>

int main() {
  using namespace demi::runtime;
  using namespace demi::runtime::isometric;
  const GridDefinition grid{
      .width = 8, .height = 8, .cellWidth = 1.0F, .cellHeight = 0.5F};
  const GridCell tile{3, 5};
  const Vec2 world = tileToWorld(grid, tile);
  if (worldToTile(grid, world) != tile) {
    std::cerr << "isometric grid/world conversion did not round-trip\n";
    return 1;
  }

  GridOccupancy occupancy(grid);
  if (!occupancy.occupy({3, 1}, {1, 5}, "wall") ||
      occupancy.occupy({3, 3}, {1, 1}, "overlap")) {
    std::cerr << "grid occupancy accepted an overlapping footprint\n";
    return 1;
  }
  const PathResult detour = findPath(occupancy, {1, 3}, {6, 3});
  if (!detour.success || detour.cells.front() != GridCell{1, 3} ||
      detour.cells.back() != GridCell{6, 3}) {
    std::cerr << "pathfinder did not produce a deterministic detour\n";
    return 1;
  }

  const PlacementResult occupied =
      evaluatePlacement(occupancy, {.origin = {3, 3}, .footprint = {1, 1}});
  if (occupied.code != "PLACEMENT_OCCUPIED") {
    std::cerr << "placement did not report occupied cells deterministically\n";
    return 1;
  }
  const PlacementResult outside =
      evaluatePlacement(occupancy, {.origin = {7, 7}, .footprint = {2, 2}});
  if (outside.code != "PLACEMENT_OUT_OF_BOUNDS") {
    std::cerr << "placement did not report grid bounds deterministically\n";
    return 1;
  }
  const PlacementResult free =
      evaluatePlacement(occupancy, {.origin = {0, 0}, .footprint = {2, 2}});
  if (!free.allowed || free.code != "OK") {
    std::cerr << "placement rejected a structurally free footprint\n";
    return 1;
  }
  return 0;
}
