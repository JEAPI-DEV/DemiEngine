#pragma once

#include <compare>
#include <cstddef>
#include <string>

namespace demi::runtime::isometric {

struct GridCell {
  int x = 0;
  int y = 0;
  auto operator<=>(const GridCell &) const = default;
};

struct GridCellHash {
  [[nodiscard]] std::size_t operator()(const GridCell &cell) const noexcept;
};

struct GridFootprint {
  int width = 1;
  int height = 1;
};

struct GridDefinition {
  int width = 0;
  int height = 0;
  float cellWidth = 1.0F;
  float cellHeight = 0.5F;
};

} // namespace demi::runtime::isometric
