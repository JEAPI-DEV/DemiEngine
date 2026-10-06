#pragma once

#include "demi/runtime/terrain/TerrainSamples.h"
#include "demi/runtime/terrain/TerrainWaterAppearance.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {

struct TerrainMasks;
struct HeightField;
namespace terrain_water_detail {
struct WaterLevelField;
}

// A water body is authored, not inferred, because the shape an author draws and
// the shape a drainage pass proposes are different intentions and one of them
// has to win visibly. Ocean, lake and river differ in how they meet the ground
// rather than only in appearance, so they are distinct kinds rather than one
// body with a flag.
enum class TerrainWaterBody { Ocean, Lake, River };

// One authored body. `id` is the stable reference gameplay and rendering use;
// a body is never identified by its position in a list.
struct TerrainWaterBodySpec {
  std::string id;
  TerrainWaterBody kind = TerrainWaterBody::Ocean;
  // World height of the surface. Authoritative for every kind: a perched river
  // or a raised lake is legitimate, so nothing infers a body's level from the
  // document's sea level.
  float level = 0;
  // River only: the channel centreline in terrain-local (x, z) with the
  // authored bed height in y, and the authored channel width along it.
  std::vector<Vec3> riverPath;
  float riverWidth = 1.F;
  // Lake/ocean only: the basin centre in terrain-local (x, z). Required for a
  // bounded body; ignored when `radius` is 0.
  Vec2 center{};
  // Zero removes the radius bound. Lakes still select the basin connected to
  // their centre; oceans cover all submerged regions in their footprint.
  float radius = 0;
  // Eases the ground into the waterline over a band, so a shore is a slope
  // rather than a cliff. Never changes which samples are water.
  bool shorelineSoftening = true;
  TerrainWaterAppearance appearance;
};

// A render-ready surface. Deliberately the only tessellated representation of a
// body: everything that answers a gameplay question reads the authored level
// and the carved ground instead, so this mesh can be culled, coarsened or
// dropped without changing what a swimming character experiences.
struct TerrainWaterSurface {
  std::string id;
  TerrainWaterBody kind = TerrainWaterBody::Ocean;
  float level = 0;
  // Terrain-local triangles clipped against ground depth and body ownership,
  // wound like the terrain mesh. Intersections add zero-depth shore vertices.
  std::vector<Vec3> vertices;
  std::vector<std::size_t> indices;
  // Flat up. Wave animation is the renderer's business; a level surface is the
  // authored truth and inventing a normal here would put render detail into the
  // shared contract.
  std::vector<Vec3> normals;
  // Metres from the water plane down to the carved ground, per vertex. Zero at
  // the shore. This is what makes depth-graded absorption and shoreline foam
  // possible without re-deriving the bathymetry at draw time.
  std::vector<float> depth;
};

// The water authoring document. Separate from TerrainRecipe on purpose: recipe
// keys are a closed, schema-validated set, and water bodies are a growing
// authoring surface that would otherwise force a recipe schema change for every
// field. `seaLevel` records the document's declared sea level so it round-trips
// and can be cross-checked; it never overrides a body's own level.
struct TerrainWaterAuthoring {
  std::vector<TerrainWaterBodySpec> bodies;
  float seaLevel = 0;
  bool authored = false;
};

struct TerrainWaterResult {
  std::shared_ptr<const terrain_water_detail::WaterLevelField> resolvedCoverage;
  // The ground after every channel, basin and shoreline in authored order. The
  // field this came from is untouched.
  TerrainSamples<float> carvedHeights;
  // One entry per accepted body, in authored order, including bodies that are
  // entirely shadowed by a later body. An empty mesh means "nothing to draw",
  // which is a different statement from "never authored".
  std::vector<TerrainWaterSurface> surfaces;
  // Bodies rejected by validation. Counted, never silently ignored, and never
  // fatal to the rest of the document.
  std::size_t dropped = 0;
};

// Non-destructive: the carve is written to a copy of the field's samples, and
// the result is returned rather than applied. `masks` may be null when no
// drainage data exists; a non-null mask set whose grid does not match the field
// is a caller error and returns nullopt rather than carving from the wrong
// resolution. Returns nullopt when the field is not a usable grid or the
// authoring is absent or corrupt.
[[nodiscard]] std::optional<TerrainWaterResult>
carveTerrainWater(const HeightField &field,
                  const TerrainWaterAuthoring &authoring,
                  const TerrainMasks *masks, std::stop_token stop = {});

// Rebuilds every accepted body's surface against the already-carved ground and
// combined authoring. `grid` supplies only size/resolution; its heights are not
// read or carved. The supplied sample buffer is preserved in the result.
// Returns nullopt for a bad grid, mismatched sample count, invalid document, or
// cancellation. This lets graph stages append a body without losing upstream
// water metadata or applying any carve twice.
[[nodiscard]] std::optional<TerrainWaterResult>
refreshTerrainWaterResult(const HeightField &grid,
                          const TerrainWaterAuthoring &combinedAuthoring,
                          const TerrainSamples<float> &alreadyCarvedHeights,
                          std::stop_token stop = {});

namespace terrain_water_detail {

// Authoring validation in one place, returning indices into
// TerrainWaterAuthoring::bodies. Carving and the gameplay queries both resolve
// their bodies through this, so a body that was dropped at carve time is
// invisible to gameplay as well. A duplicate id is detected across the whole
// document, so a rejected body still reserves its id: two broken bodies are
// better reported than one silently repaired.
[[nodiscard]] std::vector<std::size_t>
acceptedBodies(const TerrainWaterAuthoring &authoring,
               std::stop_token stop = {});

// The authored water plane over a grid, resolved in authored order so a later
// body owns every overlap.
//
// `ground` is the ground the water will sit on, and which one is passed is the
// whole contract: the original heights bound a carve, and the carved heights
// build surfaces and answer gameplay queries. Because coverage is a pure
// function of (ground, authoring), the two passes cannot disagree.
struct WaterLevelField {
  Vec2 size{};
  int cellsX = 0;
  int cellsZ = 0;
  // Parallel to the sample grid; `wet` false means no body reaches the sample,
  // and `level`/`body` are then meaningless rather than zero-valued.
  std::vector<float> level;
  std::vector<std::uint8_t> wet;
  // Index into TerrainWaterAuthoring::bodies, not into the accepted list, so a
  // caller can read the body without a second lookup.
  std::vector<std::size_t> body;
  static constexpr std::size_t noBody = ~std::size_t(0);

  [[nodiscard]] std::size_t samples() const { return level.size(); }
  [[nodiscard]] std::size_t sampleAt(int x, int z) const {
    return std::size_t(z) * (std::size_t(cellsX) + 1) + std::size_t(x);
  }
  [[nodiscard]] Vec2 position(int x, int z) const {
    return {float(double(x) * size.x / cellsX),
            float(double(z) * size.y / cellsZ)};
  }
};

[[nodiscard]] WaterLevelField
buildWaterLevelField(Vec2 size, int cellsX, int cellsZ,
                     const TerrainSamples<float> &ground,
                     const TerrainWaterAuthoring &authoring);

} // namespace terrain_water_detail
} // namespace demi::runtime
