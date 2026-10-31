#include "demi/runtime/terrain/RegionalTerrainCover.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {
TerrainRegionRectangle unit(TerrainRegionPatchKey key) {
  if (key.level > 30 || key.x >= (1U << key.level) ||
      key.z >= (1U << key.level)) {
    throw std::invalid_argument("Invalid regional quadtree key");
  }
  const double size = std::ldexp(1.0, -int(key.level));
  return {key.x * size, key.z * size, size, size};
}
int adjacent(const TerrainRegionRectangle &a, const TerrainRegionRectangle &b) {
  const bool zOverlap =
      std::max(a.z, b.z) < std::min(a.z + a.depth, b.z + b.depth);
  const bool xOverlap =
      std::max(a.x, b.x) < std::min(a.x + a.width, b.x + b.width);
  if (zOverlap && a.x == b.x + b.width) {
    return 1;
  }
  if (zOverlap && a.x + a.width == b.x) {
    return 2;
  }
  if (xOverlap && a.z == b.z + b.depth) {
    return 4;
  }
  if (xOverlap && a.z + a.depth == b.z) {
    return 8;
  }
  return 0;
}
double distance(const TerrainRegionConfig &config, TerrainRegionPatchKey key,
                Vec3 camera, const TerrainRegionHeightBounds &heightBounds) {
  const auto box = regionPatchRectangle(config, key);
  const double dx =
      camera.x - std::clamp(double(camera.x), box.x, box.x + box.width);
  const double dz =
      camera.z - std::clamp(double(camera.z), box.z, box.z + box.depth);
  const auto [minimum, maximum] = heightBounds(key);
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
    throw std::invalid_argument("Regional patch height bounds are invalid");
  }
  const double dy =
      camera.y - std::clamp(double(camera.y), double(minimum), double(maximum));
  return std::hypot(std::hypot(dx, dz), dy);
}
TerrainRegionPatchKey parent(TerrainRegionPatchKey key) {
  return {key.level - 1, key.x / 2, key.z / 2};
}
} // namespace
TerrainRegionRectangle regionPatchRectangle(const TerrainRegionConfig &config,
                                            TerrainRegionPatchKey key) {
  const auto box = unit(key);
  return {config.originX + box.x * config.recipe.size.x,
          config.originZ + box.z * config.recipe.size.y,
          box.width * config.recipe.size.x, box.depth * config.recipe.size.y};
}
std::uint32_t regionMaximumLevel(const TerrainRegionConfig &config) {
  double step = std::max(config.recipe.size.x, config.recipe.size.y) /
                config.recipe.chunkCells;
  std::uint32_t level = 0;
  while (step > config.baseSampleSpacing && level < 30) {
    step *= .5;
    ++level;
  }
  if (step > config.baseSampleSpacing) {
    throw std::invalid_argument("Regional sampling exceeds quadtree precision");
  }
  return level;
}
std::vector<TerrainRegionPatchVariant>
regionCoverVariants(std::span<const TerrainRegionPatchKey> cover) {
  std::vector<TerrainRegionPatchVariant> result;
  result.reserve(cover.size());
  const std::set<TerrainRegionPatchKey> leaves(cover.begin(), cover.end());
  for (const auto key : cover) {
    (void)unit(key);
    TerrainRegionPatchVariant variant{key, 0};
    const std::int64_t extent = std::int64_t{1} << key.level;
    const auto inspectEdge = [&](std::int64_t x, std::int64_t z,
                                 unsigned edge) {
      if (x < 0 || z < 0 || x >= extent || z >= extent) {
        return;
      }
      TerrainRegionPatchKey neighbor{key.level, std::uint32_t(x),
                                     std::uint32_t(z)};
      for (;;) {
        if (leaves.contains(neighbor)) {
          if (key.level - neighbor.level > 1) {
            throw std::invalid_argument("Regional cover must be balanced 2:1");
          }
          if (neighbor.level < key.level) {
            variant.stitch |= std::uint8_t(edge);
          }
          return;
        }
        if (neighbor.level == 0) {
          return;
        }
        neighbor = parent(neighbor);
      }
    };
    // A coarser neighbor spans this entire edge. Ascending the adjacent cell
    // finds it directly, without comparing every patch with every other patch.
    inspectEdge(std::int64_t(key.x) - 1, key.z, 1);
    inspectEdge(std::int64_t(key.x) + 1, key.z, 2);
    inspectEdge(key.x, std::int64_t(key.z) - 1, 4);
    inspectEdge(key.x, std::int64_t(key.z) + 1, 8);
    result.push_back(variant);
  }
  return result;
}
std::optional<std::vector<TerrainRegionPatchKey>>
advanceRegionCover(const TerrainRegionConfig &config,
                   std::span<const TerrainRegionPatchKey> cover, Vec3 camera,
                   const TerrainRegionHeightBounds &heightBounds, double ratio,
                   std::optional<Vec3> forward, bool allowRefinement) {
  if (!std::isfinite(ratio) || ratio <= 0 || !std::isfinite(camera.x) ||
      !std::isfinite(camera.y) || !std::isfinite(camera.z) || !heightBounds) {
    throw std::invalid_argument(
        "Regional demand must be finite with positive distance ratio");
  }
  (void)regionCoverVariants(cover);
  std::map<TerrainRegionPatchKey, int> siblings;
  for (const auto key : cover) {
    if (key.level) {
      ++siblings[parent(key)];
    }
  }
  std::optional<TerrainRegionPatchKey> merge;
  double mergeScore = 0;
  for (const auto &[key, count] : siblings) {
    if (count != 4) {
      continue;
    }
    const auto box = regionPatchRectangle(config, key);
    const double score = distance(config, key, camera, heightBounds) /
                         std::max(box.width, box.depth);
    if (score <= ratio * 1.4 || score <= mergeScore) {
      continue;
    }
    bool balanced = true;
    for (const auto other : cover) {
      if (other.level > key.level + 1 && adjacent(unit(key), unit(other))) {
        balanced = false;
      }
    }
    if (balanced) {
      merge = key;
      mergeScore = score;
    }
  }
  const auto maximum = regionMaximumLevel(config);
  std::optional<TerrainRegionPatchKey> split;
  double splitScore = 0;
  for (const auto key : cover) {
    if (!allowRefinement || key.level >= maximum) {
      continue;
    }
    const auto box = regionPatchRectangle(config, key);
    const double size = std::max(box.width, box.depth);
    const double d = distance(config, key, camera, heightBounds);
    double viewPriority = 1;
    if (forward) {
      const auto [low, high] = heightBounds(key);
      const double x = box.x + box.width * .5 - camera.x;
      const double y = (double(low) + high) * .5 - camera.y;
      const double z = box.z + box.depth * .5 - camera.z;
      const double extent = std::hypot(std::hypot(x, z), y);
      const double facing = x * forward->x + y * forward->y + z * forward->z;
      viewPriority += extent > 0 ? std::max(0.0, facing / extent) : 0;
    }
    // Refine close visible terrain first. Coarse distant coverage remains
    // present; it must not outrank the foreground solely because it is larger.
    const double score = viewPriority / (1.0 + d);
    if (d < size * ratio && score > splitScore) {
      split = key;
      splitScore = score;
    }
  }
  if (!split) {
    if (merge) {
      std::vector<TerrainRegionPatchKey> next;
      for (const auto key : cover) {
        if (key.level == merge->level + 1 && parent(key) == *merge) {
          continue;
        }
        next.push_back(key);
      }
      next.push_back(*merge);
      std::sort(next.begin(), next.end());
      return next;
    }
    return {};
  }
  bool changed;
  do {
    changed = false;
    for (const auto key : cover) {
      if (key.level < split->level && adjacent(unit(*split), unit(key))) {
        split = key;
        changed = true;
        break;
      }
    }
  } while (changed);
  std::vector<TerrainRegionPatchKey> next;
  for (const auto key : cover) {
    if (key != *split) {
      next.push_back(key);
    }
  }
  for (std::uint32_t z = 0; z < 2; ++z) {
    for (std::uint32_t x = 0; x < 2; ++x) {
      next.push_back({split->level + 1, split->x * 2 + x, split->z * 2 + z});
    }
  }
  std::sort(next.begin(), next.end());
  return next;
}
std::vector<TerrainRegionPatchKey>
planInitialRegionCover(const RegionalTerrainSampler &sampler, Vec3 camera,
                       std::size_t maximumPatches, std::stop_token stop) {
  if (stop.stop_requested()) {
    throw std::runtime_error("Initial regional cover planning cancelled");
  }
  if (maximumPatches == 0) {
    throw std::invalid_argument(
        "Initial regional cover needs a nonzero patch budget");
  }
  std::map<TerrainRegionPatchKey, std::pair<float, float>> heights;
  const TerrainRegionHeightBounds bounds = [&](TerrainRegionPatchKey key) {
    auto found = heights.find(key);
    if (found == heights.end()) {
      found = heights.emplace(key, sampler.patchHeightBounds(key, stop)).first;
    }
    return found->second;
  };
  std::vector<TerrainRegionPatchKey> cover{{0, 0, 0}};
  while (cover.size() + 3 <= maximumPatches) {
    if (stop.stop_requested()) {
      throw std::runtime_error("Initial regional cover planning cancelled");
    }
    auto next = advanceRegionCover(sampler.config(), cover, camera, bounds,
                                   sampler.config().streaming.lodDistanceRatio);
    if (!next || next->size() <= cover.size()) {
      break;
    }
    cover = std::move(*next);
  }
  return cover;
}

} // namespace demi::runtime
