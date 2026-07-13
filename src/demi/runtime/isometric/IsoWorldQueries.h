#pragma once

#include "demi/runtime/isometric/GridOccupancy.h"
#include "demi/runtime/isometric/PlacementRules.h"
#include "demi/runtime/scene/model/World.h"

#include <optional>
#include <string>

namespace demi::runtime::isometric {

[[nodiscard]] std::optional<GridDefinition> gridDefinition(const World &world);
[[nodiscard]] GridOccupancy buildOccupancy(const World &world);
[[nodiscard]] std::optional<std::string> entityAt(const World &world,
                                                  GridCell cell);
[[nodiscard]] PlacementResult
queryPlacement(const World &world, GridCell origin, GridFootprint footprint);

} // namespace demi::runtime::isometric
