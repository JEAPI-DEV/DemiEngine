#pragma once

#include "demi/runtime/terrain/TerrainWater.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

// A gameplay answer about a point. Every field is derived from the authored
// level and the carved ground; nothing here is read from a surface mesh, so a
// distant, culled or coarsely tessellated water body still answers exactly.
struct TerrainWaterQuery {
  bool submerged = false;
  // Metres below the surface, 0 when dry.
  float depth = 0;
  // Terrain-local surface and bed elevations for the sampled water column.
  float surfaceHeight = 0;
  Vec3 normal{0, 1, 0};
  float groundHeight = 0;
  // True only when the supplied local Y lies inside the water, not in air or
  // below its bed. `submerged` above describes the existence of a wet column.
  bool underwater = false;
  // The owning body, empty when dry. Ownership is the authored priority order,
  // the same order that resolved the surfaces.
  std::string bodyId;
  // Prescribed terrain-local current; independent of drainage accumulation.
  float flowSpeed = 0;
  Vec3 flowVelocity{};
};

// Positions and elevations use terrain-local coordinates. World-space callers
// must transform their position into the terrain's authored coordinate system.
// A resolved water field, built once and sampled cheaply. Rebuilding the
// authored level plane per query would be O(cells) per character per frame, and
// it is also the thing that guarantees the answers are independent of the
// render mesh: there is nothing render-side in here to read.
class TerrainWaterQueryContext {
public:
  // Nullopt when the grid is unusable or does not match the carved heights it
  // is asked to describe. A grid mismatch is reported rather than resampled:
  // silently answering from a different resolution is how gameplay and visuals
  // start disagreeing. `masks` may be null when no drainage data exists; a
  // non-null mask set on a different grid is refused for the same reason, and
  // for the same one the carve refuses it.
  [[nodiscard]] static std::optional<TerrainWaterQueryContext>
  build(const TerrainWaterAuthoring &authoring,
        const TerrainWaterResult &result, const TerrainMasks *masks, int cellsX,
        int cellsZ, Vec2 size);

  // Nullopt for a non-finite position or one outside the field. Bounds are
  // inclusive, and a point outside the field is not silently clamped to the
  // edge: a clamped answer would let a body of water that does not exist under
  // the caller report itself.
  [[nodiscard]] std::optional<TerrainWaterQuery>
  sample(Vec3 localPosition) const;

  [[nodiscard]] int cellsX() const { return cellsX_; }
  [[nodiscard]] int cellsZ() const { return cellsZ_; }
  [[nodiscard]] Vec2 size() const { return size_; }

private:
  Vec2 size_{};
  int cellsX_ = 0;
  int cellsZ_ = 0;
  // Shared pages with the result, never written: the copy is a lifetime
  // guarantee, not a snapshot of mutable state.
  TerrainSamples<float> ground_;
  std::shared_ptr<const terrain_water_detail::WaterLevelField> coverage_;
  // Indexed by authored body position. Copies own the path/current data for
  // context lifetime, independently of the authoring document's lifetime.
  std::vector<TerrainWaterBodySpec> bodies_;
};

// One point, without holding a context. Convenient for a query per event;
// a per-frame loop should build a context once instead.
[[nodiscard]] std::optional<TerrainWaterQuery>
sampleTerrainWaterAt(const TerrainWaterAuthoring &authoring,
                     const TerrainWaterResult &result,
                     const TerrainMasks *masks, Vec3 worldPosition, int cellsX,
                     int cellsZ, Vec2 size);

} // namespace demi::runtime
