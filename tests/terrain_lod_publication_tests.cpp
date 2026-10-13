#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLodBiomes.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <vector>

using namespace demi::runtime;

namespace {

using Vertex = std::array<float, 8>;
using Triangle = std::array<Vertex, 3>;

Vertex vertexAttributes(Vec3 position, Vec3 normal, Vec2 uv) {
  return {position.x, position.y, position.z, normal.x,
          normal.y,   normal.z,   uv.x,       uv.y};
}

// Vertex storage may be indexed or expanded. Compare triangles without hiding
// winding differences: only cyclic rotations represent the same oriented face.
Triangle canonicalTriangle(Triangle triangle) {
  const auto first = std::min_element(triangle.begin(), triangle.end());
  std::rotate(triangle.begin(), first, triangle.end());
  return triangle;
}

std::vector<Triangle>
publishedTriangles(const terrain_detail::SurfaceTriangles &surface) {
  std::vector<Triangle> result;
  for (std::size_t first = 0; first < surface.vertices.size(); first += 3) {
    Triangle triangle;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const std::size_t index = first + corner;
      triangle[corner] =
          vertexAttributes(surface.vertices.at(index),
                           surface.normals.at(index), surface.uvs.at(index));
    }
    result.push_back(canonicalTriangle(triangle));
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<Triangle> lodTriangles(const TerrainLodBiomeGroup &group) {
  std::vector<Triangle> result;
  for (std::size_t first = 0; first < group.indices.size(); first += 3) {
    Triangle triangle;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const std::size_t index = group.indices.at(first + corner);
      triangle[corner] =
          vertexAttributes(group.vertices.at(index), group.normals.at(index),
                           group.uvs.at(index));
    }
    result.push_back(canonicalTriangle(triangle));
  }
  std::sort(result.begin(), result.end());
  return result;
}

HeightField mixedField() {
  TerrainRecipe recipe;
  recipe.cellsX = 9;
  recipe.cellsZ = 7;
  recipe.chunkCells = 4;
  recipe.size = {27.F, 14.F};
  auto generated = TerrainGenerator::generate(recipe);
  assert(generated.has_value());
  HeightField field = std::move(*generated);
  field.biomeIds = {"rock", "sand", "grass"};
  field.biomeColors = {
      {100, 110, 120, 255}, {210, 180, 90, 255}, {40, 130, 60, 255}};
  field.biomeTextureScales = {0.25F, 2.F, 0.75F};
  for (int z = 0; z <= field.cellsZ; ++z) {
    for (int x = 0; x <= field.cellsX; ++x) {
      const std::size_t sample = field.index(x, z);
      field.heights.set(sample, float(x * x - z * z + x * z) * 0.125F);
      field.biomeIndices.set(sample, std::size_t((x + 2 * z) % 3));
    }
  }
  assert(TerrainGenerator::recomputeTerrainNormals(field));
  return field;
}

void fullLodMatchesPublishedSurfaces(const HeightField &field,
                                     const TerrainChunk &chunk) {
  const auto published = terrain_detail::buildChunkTriangles(field, chunk);
  const auto reduced =
      buildTerrainLodBiomeMesh(field, chunk, TerrainLod::Full, 0.F);
  assert(reduced.has_value());
  assert(reduced->groups.size() == published.size());
  for (const auto &[biome, surface] : published) {
    const auto group =
        std::find_if(reduced->groups.begin(), reduced->groups.end(),
                     [&](const TerrainLodBiomeGroup &candidate) {
                       return candidate.biomeId == field.biomeIds.at(biome);
                     });
    assert(group != reduced->groups.end());
    assert(lodTriangles(*group) == publishedTriangles(surface));
  }
}

} // namespace

int main() {
  const HeightField field = mixedField();
  for (const TerrainChunk &chunk : field.chunks) {
    fullLodMatchesPublishedSurfaces(field, chunk);
  }
  // Non-origin chunks with dimensions unrelated to the normal chunk size must
  // retain the same material and sample conventions as normal publication.
  fullLodMatchesPublishedSurfaces(field, {1, 1, 7, 5});
  std::cout << "Terrain LOD publication parity tests passed\n";
}
