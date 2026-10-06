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
constexpr int terrainLodIndex(TerrainLod level) { return static_cast<int>(level); }

struct TerrainLodSettings {
  // Chunks nearer than this use the level at or below the first entry. A chunk
  // at distance d is given the count of entries it has passed, so entries may
  // be raised or lowered freely without disturbing the ordering of the others.
  std::array<float, 4> distanceThresholds{40.F, 110.F, 260.F, 600.F};
  int maximumLevel = 4; // clamped to the thresholds' size
  // Below this height difference two levels are treated as equivalent, which is
  // what stops the surface visibly stepping as the camera moves.
  float geometricError = 0.35F;
};

// Number of levels the distance table may address, clamped into
// [0, min(terrainLodLevelCount - 1, distanceThresholds.size())], so no index past
// the thresholds' size can ever be produced. `maximumLevel` is a level COUNT, not
// an index: 4 allows every level including `Off`.
[[nodiscard]] int terrainLodUsableLevels(const TerrainLodSettings &settings);

// The level for a chunk, from the camera's distance to the chunk's NEAREST
// point. Nearest, not centre: a long chunk is visible well before its centre is
// close, and using the centre makes the near seam pop.
//
// No chunk is named, so this answers for the chunk NEAREST the camera, which is
// also where the geometric-error term is measured. Both parts are therefore the
// same answer terrainLodForChunk gives for that chunk, and the whole function
// costs O(chunks + cells in one chunk). A field with no chunk list is treated as
// a single chunk covering its own extent.
//
// Pure in (field, camera, settings), and never returns an index past
// `terrainLodLevelCount - 1`.
[[nodiscard]] TerrainLod selectTerrainLod(const HeightField &, Vec2 cameraPosition,
                                          const TerrainLodSettings & = {});

// The level a chunk should be STORED at. Independent of the camera, so a chunk
// that is momentarily far away is not rebuilt every frame. `maxDistance` is the
// camera's distance to the chunk's NEAREST point, evaluated by the caller
// against its own streaming radius.
//
// Cost is O(cells in the chunk) times the number of levels the geometric-error
// walk inspects, and never touches a cell outside the chunk.
[[nodiscard]] TerrainLod terrainLodForChunk(const HeightField &, const TerrainChunk &,
                                            float maxDistance,
                                            const TerrainLodSettings & = {});

// The reduced mesh for one chunk at one level.
//
// SEAM SAFETY IS THE WHOLE POINT. Adjacent chunks at different levels must
// agree on their shared edge, or a crack appears. The mechanism: the edge
// vertices of a reduced chunk carry a "skirt" down to the FULL-resolution
// height, so a level mismatch is filled by geometry rather than showing sky.
// Return the skirt explicitly so a test can assert it exists at every boundary
// where the neighbour's level differs.
//
// This is a single-group mesh. Biome grouping and UVs belong to the chunk mesh
// builder that partitions triangles per biome; a consumer that needs several
// materials has to re-partition these triangles itself.
struct TerrainLodMesh {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<std::size_t> indices;
  // For each boundary edge vertex, how far down the skirt reaches. Empty at a
  // level with no neighbour mismatch.
  std::vector<float> skirtDepths;
  // How many field cells this level represents, per emitted vertex: the linear
  // stride squared. 1 at Full, 4 at Half, 16 at Quarter, 64 at Coarse.
  std::size_t sourceCells = 0;
};

// The reduced mesh for one chunk at one level, or nothing for `TerrainLod::Off`.
//
// `skirtDepth` is the MINIMUM reach, in world units, of the skirt emitted at the
// chunk's boundary. The actual reach is raised per vertex to whatever covers the
// deepest full-resolution height along the chunk's shared edges, because a
// caller-supplied depth that is too small would reintroduce exactly the crack
// the skirt exists to hide. Pass 0 to build a mesh without a skirt, which is
// correct only when every neighbour is at the same level.
//
// `Full` never emits a skirt: its boundary vertices already ARE the
// full-resolution edge samples, so it cannot crack against a neighbour.
//
// Returns nothing for a chunk that does not fit inside the field, which keeps
// the sampler from reading outside the heightfield.
[[nodiscard]] std::optional<TerrainLodMesh>
buildTerrainLodMesh(const HeightField &, const TerrainChunk &, TerrainLod,
                     float skirtDepth);
} // namespace demi::runtime