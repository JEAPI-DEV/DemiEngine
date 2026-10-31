#include "demi/runtime/terrain/RegionalTerrainMorph.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
RegionalTerrainCoverSurface::RegionalTerrainCoverSurface(
    const TerrainRegionConfig &config,
    std::map<TerrainRegionPatchKey, std::shared_ptr<const HeightField>> fields)
    : bounds_{config.originX, config.originZ, config.recipe.size.x,
              config.recipe.size.y},
      fields_(std::move(fields)) {
  if (!std::isfinite(bounds_.x) || !std::isfinite(bounds_.z) ||
      !std::isfinite(bounds_.width) || !std::isfinite(bounds_.depth) ||
      bounds_.width <= 0 || bounds_.depth <= 0) {
    throw std::invalid_argument("Morph cover requires finite positive bounds");
  }
  for (const auto &[key, field] : fields_) {
    (void)regionPatchRectangle(config, key);
    if (!field || field->cellsX <= 0 || field->cellsZ <= 0) {
      throw std::invalid_argument("Morph cover requires valid heightfields");
    }
    maximumLevel_ = std::max(maximumLevel_, key.level);
  }
}
const HeightField *
RegionalTerrainCoverSurface::field(TerrainRegionPatchKey key) const {
  const auto found = fields_.find(key);
  return found == fields_.end() ? nullptr : found->second.get();
}
float RegionalTerrainCoverSurface::height(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) {
    throw std::invalid_argument("Morph sample position must be finite");
  }
  const double u = std::clamp((x - bounds_.x) / bounds_.width, 0.0, 1.0);
  const double v = std::clamp((z - bounds_.z) / bounds_.depth, 0.0, 1.0);
  for (std::uint32_t level = 0; level <= maximumLevel_; ++level) {
    const auto width = std::uint32_t{1} << level;
    const TerrainRegionPatchKey key{
        level, std::min(std::uint32_t(u * width), width - 1),
        std::min(std::uint32_t(v * width), width - 1)};
    const auto *samples = field(key);
    if (!samples) {
      continue;
    }
    const double scale = std::ldexp(1.0, -int(key.level));
    const TerrainRegionRectangle box{bounds_.x + key.x * scale * bounds_.width,
                                     bounds_.z + key.z * scale * bounds_.depth,
                                     scale * bounds_.width,
                                     scale * bounds_.depth};
    const double gx = std::clamp((x - box.x) / box.width * samples->cellsX, 0.0,
                                 double(samples->cellsX));
    const double gz = std::clamp((z - box.z) / box.depth * samples->cellsZ, 0.0,
                                 double(samples->cellsZ));
    const int ix = std::min(int(gx), samples->cellsX - 1);
    const int iz = std::min(int(gz), samples->cellsZ - 1);
    const double fx = gx - ix;
    const double fz = gz - iz;
    const double b = samples->height(ix + 1, iz);
    const double c = samples->height(ix, iz + 1);
    if (fx + fz <= 1.0) {
      const double a = samples->height(ix, iz);
      return float(a + fx * (b - a) + fz * (c - a));
    }
    const double d = samples->height(ix + 1, iz + 1);
    return float(d + (1 - fx) * (c - d) + (1 - fz) * (b - d));
  }
  throw std::invalid_argument("Morph source cover has a hole");
}
std::shared_ptr<const HeightField>
makeRegionalMorphField(const TerrainRegionConfig &config,
                       TerrainRegionPatchKey key, const HeightField &grid,
                       const RegionalTerrainCoverSurface &from,
                       const RegionalTerrainCoverSurface &to) {
  auto result = std::make_shared<HeightField>(grid);
  auto start = std::make_shared<TerrainSamples<float>>();
  start->resize(grid.heights.size());
  const auto *exactFrom = from.field(key);
  const auto *exactTo = to.field(key);
  const auto box = regionPatchRectangle(config, key);
  for (int z = 0; z <= grid.cellsZ; ++z) {
    for (int x = 0; x <= grid.cellsX; ++x) {
      const auto index = grid.index(x, z);
      const double worldX = box.x + double(x) * box.width / grid.cellsX;
      const double worldZ = box.z + double(z) * box.depth / grid.cellsZ;
      start->set(index, exactFrom ? exactFrom->height(x, z)
                                  : from.height(worldX, worldZ));
      result->heights.set(index, exactTo ? exactTo->height(x, z)
                                         : to.height(worldX, worldZ));
    }
  }
  result->morphFromHeights = std::move(start);
  return result;
}
} // namespace demi::runtime
