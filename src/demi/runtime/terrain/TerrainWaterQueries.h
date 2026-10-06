#pragma once

#include "demi/runtime/terrain/TerrainWater.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

// Gameplay policy, named so a project can see the assumption its queries are
// made under. Shallower than this is a shoreline, not somewhere to swim, and
// faster than this is a rapid: both answers are about the player, not about the
// renderer, which is why they are gameplay-side constants and not shading.
inline constexpr float terrainWaterMinSwimDepth = 1.5F;
inline constexpr float terrainWaterMaxSwimFlow = 2.5F;
// The speed a fully accumulated drainage cell is reported as flowing. The
// drainage mask is a normalised accumulation, not a measurement, so the scale
// is a declared convention and never a silent per-mask guess.
inline constexpr float terrainWaterReferenceFlowSpeed = 6.F;

// A gameplay answer about a point. Every field is derived from the authored
// level and the carved ground; nothing here is read from a surface mesh, so a
// distant, culled or coarsely tessellated water body still answers exactly.
struct TerrainWaterQuery {
  bool submerged = false;
  // Metres below the surface, 0 when dry.
  float depth = 0;
  // Terrain-local surface and bed elevations for the sampled water column.
  float surfaceHeight = 0;
  float groundHeight = 0;
  // True only when the supplied local Y lies inside the water, not in air or
  // below its bed. `submerged` above describes the existence of a wet column.
  bool underwater = false;
  // The owning body, empty when dry. Ownership is the authored priority order,
  // the same order that resolved the surfaces.
  std::string bodyId;
  bool swimmable = false;
  // From the drainage field. Zero in a lake or an ocean, which are still by
  // definition, and zero wherever no drainage data exists.
  float flowSpeed = 0;
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
  TerrainSamples<float> flow_;
  // Indexed by the body's position in the authoring document, so an owned
  // sample names its body without a second lookup.
  std::vector<std::string> ids_;
  std::vector<TerrainWaterBody> kinds_;
};

// One point, without holding a context. Convenient for a query per event;
// a per-frame loop should build a context once instead.
[[nodiscard]] std::optional<TerrainWaterQuery>
sampleTerrainWaterAt(const TerrainWaterAuthoring &authoring,
                     const TerrainWaterResult &result,
                     const TerrainMasks *masks, Vec3 worldPosition, int cellsX,
                     int cellsZ, Vec2 size);

} // namespace demi::runtime
