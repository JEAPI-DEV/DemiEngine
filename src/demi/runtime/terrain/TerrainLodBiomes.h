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

struct TerrainLodBiomeGroup {
  std::string biomeId;
  Color tint;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  // Local world distance times biome texture scale, matching Full publication.
  std::vector<Vec2> uvs;
  std::vector<std::size_t> indices;
  // Full-grid sample corresponding to each vertex; skirt bottoms reuse the
  // top sample. Material-role classification can consume the same provenance.
  std::vector<std::size_t> sampleIndices;
  // Geometry provenance assigns each coarse cell's area once to its first
  // triangle. This counts covered source cells, not biome area: a second Full
  // triangle may belong to another group that receives no area count.
  std::size_t sourceCells = 0;
  // Skirt bottom vertices owned once across all groups, in triangle order.
  std::size_t skirtDepths = 0;
};

struct TerrainLodBiomeMesh {
  // Recipe biome order, omitting identities that receive no triangles.
  std::vector<TerrainLodBiomeGroup> groups;
  // Nominal cell stride squared, matching TerrainLodMesh.
  std::size_t sourceCells = 0;
  // Source area assigned to triangles with an unnamed biome owner.
  std::size_t skippedBiomes = 0;
  [[nodiscard]] bool empty() const noexcept { return groups.empty(); }
};

// Votes Full-triangle biome owners in the stride-aligned cell rectangle.
// Ties use the lowest biome index. Raw sample labels with no Full surface
// ownership are never introduced as reduced surfaces. Invalid cells, strides
// and fields, or a region without a named triangle owner, throw
// invalid_argument.
[[nodiscard]] std::size_t terrainLodBiomeForCell(const HeightField &, int cellX,
                                                 int cellZ, int step);

// Partitions explicit geometry topology/provenance without reconstructing its
// lattice. Full uses the exact authored publication triangle-majority rule;
// reduced triangles vote Full owners inside their source rectangle. Geometry,
// winding and canonical Full normals are unchanged by partitioning. Groups
// retain world-scale biome UVs and sample provenance for material-role binding.
// Off returns nothing. Invalid requests throw invalid_argument.
[[nodiscard]] std::optional<TerrainLodBiomeMesh>
buildTerrainLodBiomeMesh(const HeightField &, const TerrainChunk &, TerrainLod,
                         float skirtDepth);

} // namespace demi::runtime
