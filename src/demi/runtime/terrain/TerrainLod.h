#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace demi::runtime {
struct HeightField;
struct TerrainChunk;

// Mesh detail levels, coarsest last. Ordered so a level's ordinal is a usable
// index: `Full` is 0 and `Off` is the highest.
enum class TerrainLod { Full, Half, Quarter, Coarse, Off };

// Number of detail levels addressable by the distance table, which is the size
// of `TerrainLodSettings::distanceThresholds` plus `Off`.
inline constexpr int terrainLodLevelCount = 5;

// Linear cell stride between samples at each level. Every stride is an exact
// power of two (or one) in CELLS, so a reduced level's samples land on real
// heightfield samples instead of drifting between them.
constexpr int terrainLodStep(TerrainLod level) {
  switch (level) {
  case TerrainLod::Full:
    return 1;
  case TerrainLod::Half:
    return 2;
  case TerrainLod::Quarter:
    return 4;
  case TerrainLod::Coarse:
    return 8;
  case TerrainLod::Off:
    break;
  }
  return 0;
}

std::string_view terrainLodName(TerrainLod level);

// Level ordinal, matching the declaration order above.
constexpr int terrainLodIndex(TerrainLod level) {
  return static_cast<int>(level);
}

struct TerrainLodSettings {
  // Chunks nearer than this use the level at or below the first entry. A chunk
  // at distance d is given the count of entries it has passed, so entries may
  // be raised or lowered freely without disturbing the ordering of the others.
  std::array<float, 4> distanceThresholds{40.F, 110.F, 260.F, 600.F};
  int maximumLevel = 4; // clamped to the thresholds' size
  // Maximum vertical deviation from the Full surface in terrain-local units.
  // This bounds shape changes; it does not provide temporal blending.
  float geometricError = 0.35F;
};

// Highest permitted level index, clamped to the distance table: zero permits
// Full only, while four permits every level including Off.
[[nodiscard]] int terrainLodUsableLevels(const TerrainLodSettings &settings);

// The level for a chunk, from the camera's distance to the chunk's NEAREST
// point. Nearest, not centre: a long chunk is visible well before its centre is
// close, and using the centre makes the near seam pop.
//
// No chunk is named, so this answers for the chunk NEAREST the camera, which is
// also where the geometric-error term is measured. Both parts are therefore the
// same answer terrainLodForChunk gives for that chunk, and the whole function
// costs O(chunks + cells in one chunk). A field with no chunk list is treated
// as a single chunk covering its own extent.
//
// Pure in (field, camera, settings), and never returns an index past
// `terrainLodLevelCount - 1`.
[[nodiscard]] TerrainLod selectTerrainLod(const HeightField &,
                                          Vec2 cameraPosition,
                                          const TerrainLodSettings & = {});

// Selects from a caller-supplied distance to the chunk's nearest point. This
// query performs geometry work; it neither caches meshes nor manages residency.
// A live frame selector must consume prepared errors instead of calling it
// repeatedly against unchanged height samples.
//
// Cost is O(cells in the chunk) times the number of levels the geometric-error
// walk inspects, and never touches a cell outside the chunk.
[[nodiscard]] TerrainLod terrainLodForChunk(const HeightField &,
                                            const TerrainChunk &,
                                            float maxDistance,
                                            const TerrainLodSettings & = {});

// Provenance of one emitted triangle. Surface refinement retains its coarse
// cell rectangle. Its cell area is counted once, on the first emitted triangle;
// other surface triangles and supplemental skirts carry zero sourceCells.
struct TerrainLodTriangle {
  int firstCellX = 0;
  int firstCellZ = 0;
  int cellsX = 0;
  int cellsZ = 0;
  std::size_t sourceCells = 0;
  bool skirt = false;
};

// The reduced mesh for one chunk at one level.
//
// Reduced interiors retain every full-resolution perimeter sample and refine
// boundary triangles to join them. Adjacent levels share the same edge
// positions and field normals, including peaks and valleys between coarse
// samples. Optional skirts supplement this topology; seam safety does not
// require them.
//
// This is a single-group mesh. Biome grouping and UVs belong to the chunk mesh
// builder that partitions triangles per biome; a consumer that needs several
// materials has to re-partition these triangles itself.

struct TerrainLodMesh {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<std::size_t> indices;
  // HeightField sample identity for every vertex; skirt bottoms repeat their
  // top.
  std::vector<std::size_t> sampleIndices;
  // One provenance entry per index triple, in the same order.
  std::vector<TerrainLodTriangle> triangles;
  // Optional skirt depths, in fine perimeter order. Empty when skirts are
  // disabled or the level is Full.
  std::vector<float> skirtDepths;
  // Nominal interior lattice stride squared: 1 at Full, 4 at Half, 16 at
  // Quarter, 64 at Coarse. Fine perimeter vertices retain one-sample spacing.
  std::size_t sourceCells = 0;
};

// The reduced mesh for one chunk at one level, or nothing for
// `TerrainLod::Off`.
//
// `skirtDepth` is the MINIMUM reach, in terrain-local units, of the skirt
// emitted at the chunk's boundary. The actual reach also covers the deepest
// edge sample. Pass zero for seam-safe surface geometry without supplemental
// skirts.
//
// `Full` never emits a skirt: its boundary vertices already ARE the
// full-resolution edge samples, so it cannot crack against a neighbour.
//
// Returns nothing for a chunk that does not fit inside the field, which keeps
// the sampler from reading outside the heightfield.
[[nodiscard]] std::optional<TerrainLodMesh>
buildTerrainLodMesh(const HeightField &, const TerrainChunk &, TerrainLod,
                    float skirtDepth);
// Exact surface-overlay height deviation in terrain-local units. Includes
// intersections of Full and reduced triangle diagonals, not only grid samples.
// Intended for preparation/cache construction, not repeated frame updates.
// Computed in double precision and rounded upward to float; planar surfaces
// may retain floating-point residuals, so use a small nonzero error tolerance.
[[nodiscard]] float terrainLodGeometricError(const HeightField &,
                                             const TerrainChunk &, TerrainLod);
[[nodiscard]] std::size_t terrainLodSurfaceTriangleCount(int cellsX, int cellsZ,
                                                         TerrainLod);
} // namespace demi::runtime
