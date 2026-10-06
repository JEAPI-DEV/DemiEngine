#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainLod.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {
struct HeightField;
struct TerrainChunk;

// One draw group: the vertices and indices for a single biome at one LOD level.
//
// This is the missing half of TerrainLodMesh. The flat reduced mesh carries
// geometry but has no notion of which biome any of it belongs to, so a chunk
// drawn from it has to pick ONE material for the whole chunk and loses every
// other biome's appearance the moment the camera pulls back. A group here is
// that material's slice of the reduced chunk, ready to be uploaded as its own
// mesh with its own tint.
//
// A group's own vertices are the reduced vertices its triangles reference, so a
// boundary vertex shared by two biomes is duplicated once per group. That is
// what a partitioned draw needs: each group is self-contained and indexable
// without a cross-group lookup.
struct TerrainLodBiomeGroup {
  // Exactly one of HeightField::biomeIds, so it names the material the same way
  // the full-resolution surface does.
  std::string biomeId;
  // Exactly one of HeightField::biomeColors, carried here so the shader needs no
  // biome lookup table. A field whose labels and colors disagree leaves the
  // default tint in place rather than inventing a color.
  Color tint;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  // World-scale, derived from the field's own extent rather than from the
  // group's vertex range, which is what keeps a reduced chunk's texel density
  // identical to the full-resolution one.
  std::vector<Vec2> uvs;
  std::vector<std::size_t> indices;
  // Full-resolution cells whose reduced cell voted for this biome. Every cell in
  // the chunk votes for exactly one biome, so these sum to the chunk's cells.
  std::size_t sourceCells = 0;
  // Skirt vertices this group OWNS: the boundary segments whose cell voted for
  // this biome. A skirt bottom standing on a biome boundary is referenced by two
  // adjacent walls and therefore appears in both groups' vertex arrays, but it
  // is owned once, so these counts still sum to the chunk's skirt vertex total.
  std::size_t skirtDepths = 0;
};

// Every biome's slice of one chunk at one level.
struct TerrainLodBiomeMesh {
  // In biomeIds order, and only for biomes that actually received geometry, so
  // the draw order depends on the recipe's biome order rather than on which cell
  // happened to vote first. Two runs over one field always produce this order.
  std::vector<TerrainLodBiomeGroup> groups;
  // Cells per emitted vertex: the level's stride squared, matching
  // TerrainLodMesh::sourceCells.
  std::size_t sourceCells = 0;
  // Cells whose biome label has no id or tint in the field, so no group could
  // carry them and their triangles were not emitted. Non-zero means the field's
  // biome labels and its biomeIds disagree, which is a corrupt field rather than
  // a streaming condition.
  std::size_t skippedBiomes = 0;
  [[nodiscard]] bool empty() const noexcept { return groups.empty(); }
};

// The biome index that owns the reduced cell a full-resolution cell falls in,
// counted over every sample inside that reduced cell. Ties go to the LOWER
// index, so a cell on a boundary lands on the same side on every run and the
// same field always splits the same way. Matches the majority label the
// full-resolution chunk builder takes for each triangle, extended from one
// triangle to a whole reduced cell: a reduced cell is the unit a vertex belongs
// to, and it has to belong to exactly one biome.
//
// Reduced cells are anchored to the field's own grid, at multiples of `step`, so
// the answer does not depend on which chunk asked. A chunk whose first cell is a
// whole number of strides into the field therefore agrees with the partition its
// own mesh used.
//
// Throws std::invalid_argument for a non-positive `step` or a cell outside the
// field, rather than answering for a rectangle that is not there.
[[nodiscard]] std::size_t terrainLodBiomeForCell(const HeightField &, int cellX,
                                                 int cellZ, int step);

// The chunk's reduced mesh, partitioned per biome, or nothing for
// `TerrainLod::Off`.
//
// Geometry is taken from `buildTerrainLodMesh` for the same chunk, level and
// skirt depth, so a partitioned chunk has the same vertices, the same normals,
// the same winding and the same skirts as the flat one, and no skirt is dropped
// by the split. Each triangle is then emitted into the group that owns its
// reduced cell, which is the same majority-vote rule the full-resolution chunk
// builder uses; every triangle of a reduced cell shares that cell's biome, so the
// split cannot disagree with the flat mesh's surface.
//
// Throws std::invalid_argument for a level outside `TerrainLod` (other than
// `Off`, which draws nothing), for a chunk that does not fit inside the field, and
// for a field that names no biome, has no extent, or has buffers that do not cover
// its own grid: all three would otherwise answer with a mesh whose groups were
// silently wrong. A refused request is a named failure, never a mesh with no groups
// in it. Cost is O(cells in the chunk); nothing outside the chunk is read.
[[nodiscard]] std::optional<TerrainLodBiomeMesh>
buildTerrainLodBiomeMesh(const HeightField &, const TerrainChunk &, TerrainLod,
                         float skirtDepth);
} // namespace demi::runtime