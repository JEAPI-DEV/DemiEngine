#pragma once

#include "demi/runtime/terrain/TerrainGeneration.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace demi::runtime {

// Knobs the constraints cannot infer from the field itself.
//
// A bound is a decision, not a measurement: "too steep to plant a tree" belongs
// to the project, not to the generator, and a constraint class that picked its
// own would make an authored scene impossible to predict.
struct TerrainScatterConstraintOptions {
  // Degrees, from TerrainMasks::slope. A cell at or above this is steep ground.
  float maximumSlopeDegrees = 40;
  // A terrain exclusion sample at or above this weight is treated as authored
  // exclusion paint. The default is deliberately tiny but non-zero: an exact
  // zero test would treat a brush that barely touched a cell as unpainted, which
  // is indistinguishable from a rounding artefact in the authored document.
  float terrainExclusionThreshold = 0.001F;
  // Same threshold for weights added through addExclusionWeight.
  float paintedExclusionThreshold = 0.001F;
  // Consulted once per cell while building, when the caller owns authored water
  // surfaces (TerrainWaterResult / TerrainWaterQueryContext).
  //
  // This is an explicit test rather than a dependency on the water types
  // because a constraint must be buildable from a heightfield alone: the water
  // stage may not have run, may be previewing, or may live in a module the
  // caller does not link. A caller with no water surfaces leaves it empty and
  // gets the sea-level test alone.
  std::function<bool(Vec3 worldPosition, float groundHeight)> submerged;
};

// Regions a placement may not land in, evaluated independently of the scatter
// solver so a constraint is authored, previewed and tested on its own.
//
// This is a value, not a service: it holds the sample counts and the spatial
// index it needs, so a caller can hold one next to the field it describes
// without keeping the field alive, and two constraint sets for two previews
// cannot interfere.
//
// The class is deliberately dumb about *why* a region is protected. A reserved
// archaeological site, a road and a brush stroke all reduce to the same query,
// so an author gets one behaviour to learn instead of one per protection kind.
//
// Constraints never relax each other. Adding one can only remove placements,
// which is the only way a constraint can be authored without re-reading
// everything that already excluded something.
class TerrainScatterConstraints {
public:
  // Samples the field's own exclusions, water level and slope masks, and
  // prepares the spatial index for authored shapes.
  //
  // The water level is the authored one when `water.authored` and the level
  // derived from the recipe's landforms otherwise, because an authored level
  // must win over a derived one exactly as an authored protection layer does.
  // Water surfaces are consulted through options.submerged and evaluated here,
  // not per query: the test may be expensive, and a caller that changes the
  // water rebuilds the constraints rather than expecting live answers.
  //
  // `masks` may be null when no masks stage ran, which disables the slope
  // constraint instead of fabricating a flat field. A non-null mask set on a
  // different grid is a caller error and throws std::invalid_argument rather
  // than resampling, because silently measuring slope on the wrong resolution
  // is how a constraint ends up "not working" with no explanation.
  [[nodiscard]] static TerrainScatterConstraints
  build(const HeightField &field, const TerrainRecipe &recipe,
        const TerrainMasks *masks, const TerrainWaterLevel &water,
        const TerrainScatterConstraintOptions &options = {});

  // A cell is excluded if ANY constraint rejects it. Constraints never relax
  // each other, so adding one can only remove placements.
  //
  // A cell outside the field's grid is never blocked: there is no ground under
  // it to test, and inventing an answer would report a constraint failure for a
  // cell the generator cannot produce.
  [[nodiscard]] bool blocked(std::size_t cell) const;

  // Why a cell is blocked, for the editor and for diagnostics. Empty when it is
  // not blocked, so an empty string is a usable "allowed" answer.
  //
  // When several constraints reject the same cell, the first in this fixed
  // precedence is reported, and the order is part of the contract because an
  // editor that re-sorts it would show a different explanation for the same
  // gap on every rebuild:
  //   footprint > road > protected area > painted exclusion >
  //   terrain exclusion > water surface > water > slope.
  //
  // A footprint outranks a road because the building is the more specific
  // authored fact; water outranks slope because "it is a lake" is a more
  // useful answer than "it is steep" for the same underwater cliff.
  [[nodiscard]] std::string_view reason(std::size_t cell) const;

  // Every blocked cell, for a coverage preview or a density report. Cached and
  // invalidated by the mutators, because it is a whole-grid scan and an editor
  // may ask for it every frame.
  [[nodiscard]] std::size_t blockedCount() const;

  // Reserved areas, roads and building footprints all reduce to the same shape.
  //
  // A shape is stored once and indexed into the buckets its extent covers, so a
  // query touches only the shapes that can possibly reach the cell.
  // Non-finite input and a non-positive radius/width are ignored rather than
  // rejected: an in-progress brush is not an error, it is not a region yet.
  void addProtectedArea(Vec2 center, float radius);
  // `centreline` is terrain-local (x, y, z) with y the authored road height,
  // which is recorded but not tested against, because the constraint answers
  // "is this cell under the road", not "is the ground high enough to be on it".
  // `width` is the full authored width, so the tested reach is half of it. A
  // centreline with fewer than two points contributes nothing.
  void addRoad(std::vector<Vec3> centreline, float width);
  // Terrain-local (x, z) polygon, any winding. Fewer than three points
  // contributes nothing; a point on an edge counts as inside, because a
  // footprint is solid and a tree half in a wall is still in the wall.
  void addFootprint(std::vector<Vec2> polygon);

  // Paint-on authoring goes through the same list, so an exclusion brush and a
  // declared road are checked identically.
  //
  // The weight is compared against options.paintedExclusionThreshold, which is
  // fixed at build time; a weight below it is recorded as no exclusion at all
  // rather than as a pending one, so there is no state to forget. A cell
  // outside the grid is ignored.
  void addExclusionWeight(std::size_t cell, float weight);

  [[nodiscard]] std::size_t cellCount() const noexcept { return mask_.size(); }
  [[nodiscard]] float seaLevel() const noexcept { return seaLevel_; }
  [[nodiscard]] float maximumSlopeDegrees() const noexcept {
    return maximumSlope_;
  }
  // Shapes registered through the three add* calls, for a diagnostics summary.
  [[nodiscard]] std::size_t shapeCount() const noexcept { return shapes_.size(); }

private:
  // Blocking kinds, in the reason precedence order above. The bit index is the
  // reason index, so resolving a reason is a first-set-bit scan and no table of
  // combinations can drift out of order.
  enum Block : std::uint8_t {
    blockFootprint = 1U << 0,
    blockRoad = 1U << 1,
    blockProtectedArea = 1U << 2,
    blockPaintedExclusion = 1U << 3,
    blockTerrainExclusion = 1U << 4,
    blockWaterSurface = 1U << 5,
    blockWater = 1U << 6,
    blockSlope = 1U << 7,
  };

  enum class ShapeKind { Area, Road, Footprint };
  struct Shape {
    ShapeKind kind = ShapeKind::Area;
    Vec2 center{};
    float radius = 0;
    std::vector<Vec3> centreline;
    float halfWidth = 0;
    std::vector<Vec2> polygon;
  };

  // World XZ of a sample, without a HeightField to call index() on.
  [[nodiscard]] Vec2 cellPosition(std::size_t cell) const noexcept;
  void insertShape(std::uint32_t shape, float minX, float maxX, float minZ,
                   float maxZ);
  [[nodiscard]] std::uint8_t spatialBlockers(Vec2 point) const;
  [[nodiscard]] std::uint8_t cellBlockers(std::size_t cell) const;
  [[nodiscard]] std::size_t countBlocked() const;

  Vec2 size_{};
  int cellsX_ = 0;
  int cellsZ_ = 0;
  // One uniform bucket grid over the whole field, independent of the sample
  // grid. A shape is registered in every bucket its extent reaches, so a query
  // reads exactly one bucket: no neighbour scan, and no per-cell cost that
  // grows with the number of authored shapes.
  int buckets_ = 1;
  float bucketSizeX_ = 1;
  float bucketSizeZ_ = 1;
  float seaLevel_ = 0;
  float maximumSlope_ = 40;
  float paintedExclusionThreshold_ = 0.001F;
  // One byte per sample: the sampled kinds (water, slope, exclusions) resolved
  // once at build time. Authored shapes are deliberately absent, because they
  // can be added after the build.
  std::vector<std::uint8_t> mask_;
  std::vector<float> painted_;
  std::vector<Shape> shapes_;
  std::vector<std::vector<std::uint32_t>> shapeBuckets_;
  mutable std::size_t cachedBlocked_ = 0;
  mutable bool blockedDirty_ = true;
};

} // namespace demi::runtime
