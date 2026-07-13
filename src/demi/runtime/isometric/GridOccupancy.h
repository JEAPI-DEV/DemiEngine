#pragma once

#include "demi/runtime/isometric/GridTypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::isometric {

class GridOccupancy {
public:
  explicit GridOccupancy(GridDefinition definition);

  [[nodiscard]] const GridDefinition &definition() const;
  [[nodiscard]] bool occupy(GridCell origin, GridFootprint footprint,
                            const std::string &entityId);
  void release(const std::string &entityId);
  [[nodiscard]] bool isFree(GridCell origin, GridFootprint footprint,
                            const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::optional<std::string> occupant(GridCell cell) const;
  [[nodiscard]] std::vector<GridCell> cellsFor(GridCell origin,
                                               GridFootprint footprint) const;

private:
  GridDefinition definition_;
  std::unordered_map<GridCell, std::string, GridCellHash> occupants_;
};

} // namespace demi::runtime::isometric
