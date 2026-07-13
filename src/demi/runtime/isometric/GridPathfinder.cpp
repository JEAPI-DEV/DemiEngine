#include "demi/runtime/isometric/GridPathfinder.h"

#include "demi/runtime/isometric/IsoGridMath.h"

#include <algorithm>
#include <array>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::isometric {

PathResult findPath(const GridOccupancy &occupancy, const GridCell start,
                    const GridCell goal, const std::string &movingEntityId) {
  if (!contains(occupancy.definition(), start))
    return {.diagnostic = "PATH_START_OUT_OF_BOUNDS", .cells = {}};
  if (!contains(occupancy.definition(), goal))
    return {.diagnostic = "PATH_GOAL_OUT_OF_BOUNDS", .cells = {}};
  if (!occupancy.isFree(goal, {1, 1}, movingEntityId))
    return {.diagnostic = "PATH_GOAL_OCCUPIED", .cells = {}};

  constexpr std::array<GridCell, 4> Directions{
      {{0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
  std::queue<GridCell> frontier;
  std::unordered_set<GridCell, GridCellHash> visited;
  std::unordered_map<GridCell, GridCell, GridCellHash> previous;
  frontier.push(start);
  visited.insert(start);
  while (!frontier.empty()) {
    const GridCell current = frontier.front();
    frontier.pop();
    if (current == goal)
      break;
    for (const GridCell direction : Directions) {
      const GridCell next{.x = current.x + direction.x,
                          .y = current.y + direction.y};
      if (visited.contains(next) || !contains(occupancy.definition(), next) ||
          !occupancy.isFree(next, {1, 1}, movingEntityId))
        continue;
      visited.insert(next);
      previous[next] = current;
      frontier.push(next);
    }
  }
  if (!visited.contains(goal))
    return {.diagnostic = "PATH_UNREACHABLE", .cells = {}};

  std::vector<GridCell> path;
  for (GridCell cell = goal;; cell = previous.at(cell)) {
    path.push_back(cell);
    if (cell == start)
      break;
  }
  std::ranges::reverse(path);
  return {.success = true, .diagnostic = "OK", .cells = std::move(path)};
}

} // namespace demi::runtime::isometric
