#include "demi/runtime/terrain/TerrainHeightField.h"

#include <stdexcept>

namespace demi::runtime {
std::size_t HeightField::index(int x, int z) const {
  if (x < 0 || z < 0 || x > cellsX || z > cellsZ)
    throw std::out_of_range("Terrain sample outside grid");
  return std::size_t(z) * (std::size_t(cellsX) + 1) + std::size_t(x);
}
Vec2 HeightField::position(int x, int z) const {
  (void)index(x, z);
  return {float(double(x) * size.x / cellsX),
          float(double(z) * size.y / cellsZ)};
}
float HeightField::height(int x, int z) const {
  return heights.at(index(x, z));
}
Vec3 HeightField::normal(int x, int z) const { return normals.at(index(x, z)); }
} // namespace demi::runtime
