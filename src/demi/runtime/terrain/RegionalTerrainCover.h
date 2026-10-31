#pragma once
#include "demi/runtime/terrain/RegionalTerrainSampler.h"
#include <compare>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>

namespace demi::runtime {
struct TerrainRegionPatchKey {
  std::uint32_t level = 0, x = 0, z = 0;
  auto operator<=>(const TerrainRegionPatchKey &) const = default;
};
struct TerrainRegionPatchVariant {
  TerrainRegionPatchKey patch;
  // Left, right, near Z, far Z edges bordering a one-level-coarser patch.
  std::uint8_t stitch = 0;
  auto operator<=>(const TerrainRegionPatchVariant &) const = default;
};
struct TerrainRegionRectangle {
  double x = 0, z = 0, width = 0, depth = 0;
};
[[nodiscard]] TerrainRegionRectangle
regionPatchRectangle(const TerrainRegionConfig &, TerrainRegionPatchKey);
[[nodiscard]] std::uint32_t regionMaximumLevel(const TerrainRegionConfig &);
[[nodiscard]] std::vector<TerrainRegionPatchVariant>
    regionCoverVariants(std::span<const TerrainRegionPatchKey>);
using TerrainRegionHeightBounds =
    std::function<std::pair<float, float>(TerrainRegionPatchKey)>;

// Plans the opening cover on a worker without constructing/publishing each
// intermediate mesh. The leaf bound is derived from the caller's payload
// budget.
[[nodiscard]] std::vector<TerrainRegionPatchKey>
planInitialRegionCover(const RegionalTerrainSampler &sampler, Vec3 camera,
                       std::size_t maximumPatches, std::stop_token stop = {});

// Proposes one balanced split/merge. Caller keeps its current complete cover
// until every variant in the returned cover is ready for atomic publication.
[[nodiscard]] std::optional<std::vector<TerrainRegionPatchKey>>
advanceRegionCover(const TerrainRegionConfig &,
                   std::span<const TerrainRegionPatchKey> cover, Vec3 camera,
                   const TerrainRegionHeightBounds &heightBounds,
                   double distanceRatio, std::optional<Vec3> forward = {},
                   bool allowRefinement = true);
} // namespace demi::runtime
