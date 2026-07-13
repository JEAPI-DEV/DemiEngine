#include "demi/runtime/isometric/GridTypes.h"

#include <cstdint>

namespace demi::runtime::isometric {

std::size_t GridCellHash::operator()(const GridCell &cell) const noexcept {
  const auto x = static_cast<std::uint32_t>(cell.x);
  const auto y = static_cast<std::uint32_t>(cell.y);
  return static_cast<std::size_t>((static_cast<std::uint64_t>(x) << 32U) | y);
}

} // namespace demi::runtime::isometric
