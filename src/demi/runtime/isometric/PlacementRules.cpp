#include "demi/runtime/isometric/PlacementRules.h"

#include "demi/runtime/isometric/IsoGridMath.h"

namespace demi::runtime::isometric {

PlacementResult evaluatePlacement(const GridOccupancy &occupancy,
                                  const PlacementRequest &request) {
  for (const GridCell cell :
       occupancy.cellsFor(request.origin, request.footprint)) {
    if (!contains(occupancy.definition(), cell))
      return {.code = "PLACEMENT_OUT_OF_BOUNDS",
              .message = "The footprint extends outside the grid."};
    if (occupancy.occupant(cell))
      return {.code = "PLACEMENT_OCCUPIED",
              .message = "A blocking entity already occupies the footprint."};
  }
  return {.allowed = true, .code = "OK", .message = "Placement is valid."};
}

} // namespace demi::runtime::isometric
