#include "demi/runtime/terrain/TerrainBrushBounds.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {

TerrainRect terrainBrushSampleBounds(const HeightField &field, Vec2 center,
                                     float radius, int border) {
  if (border < 0)
    throw std::invalid_argument("Terrain brush border must be nonnegative");
  const double minimumX = double(center.x) - radius;
  const double maximumX = double(center.x) + radius;
  const double minimumZ = double(center.y) - radius;
  const double maximumZ = double(center.y) + radius;
  if (maximumX < 0 || maximumZ < 0 || minimumX > field.size.x ||
      minimumZ > field.size.y)
    return {};
  const auto sample = [](double coordinate, float extent, int cells,
                         bool upper) {
    const double value = coordinate / extent * cells;
    return int(std::clamp(upper ? std::ceil(value) : std::floor(value), 0.0,
                          double(cells)));
  };
  return TerrainRect{sample(minimumX, field.size.x, field.cellsX, false),
                     sample(minimumZ, field.size.y, field.cellsZ, false),
                     sample(maximumX, field.size.x, field.cellsX, true),
                     sample(maximumZ, field.size.y, field.cellsZ, true)}
      .expanded(border, field.cellsX, field.cellsZ);
}

} // namespace demi::runtime
