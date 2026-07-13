#pragma once

#include "demi/runtime/isometric/GridOccupancy.h"

#include <string>
#include <vector>

namespace demi::runtime::isometric {

struct PathResult {
  bool success = false;
  std::string diagnostic;
  std::vector<GridCell> cells;
};

[[nodiscard]] PathResult findPath(const GridOccupancy &occupancy,
                                  GridCell start, GridCell goal,
                                  const std::string &movingEntityId = {});

} // namespace demi::runtime::isometric
