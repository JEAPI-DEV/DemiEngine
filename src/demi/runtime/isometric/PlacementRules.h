#pragma once

#include "demi/runtime/isometric/GridOccupancy.h"

#include <string>

namespace demi::runtime::isometric {

struct PlacementRequest {
  GridCell origin;
  GridFootprint footprint;
};

struct PlacementResult {
  bool allowed = false;
  std::string code;
  std::string message;
};

[[nodiscard]] PlacementResult
evaluatePlacement(const GridOccupancy &occupancy,
                  const PlacementRequest &request);

} // namespace demi::runtime::isometric
