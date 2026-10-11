#include "demi/runtime/terrain/TerrainGeometryUv.h"

#include <cmath>
#include <stdexcept>

namespace demi::runtime::terrain_detail {

Vec2 terrainUv(Vec2 position, float textureScale) {
  if (!std::isfinite(textureScale) || textureScale <= 0) {
    throw std::invalid_argument(
        "Terrain texture_scale must be finite and positive");
  }
  const auto u = float(double(position.x) * textureScale);
  const auto v = float(double(position.y) * textureScale);
  if (!std::isfinite(u) || !std::isfinite(v)) {
    throw std::invalid_argument(
        "Terrain texture_scale produces non-finite UVs");
  }
  return {u, v};
}

} // namespace demi::runtime::terrain_detail
