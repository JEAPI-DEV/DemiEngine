#include "demi/runtime/terrain/TerrainGeometryUv.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainLodBiomes.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace demi::runtime;

namespace {

constexpr std::array Levels{TerrainLod::Full, TerrainLod::Half,
                            TerrainLod::Quarter, TerrainLod::Coarse};

template <class Label> HeightField makeField(int cells, Label label) {
  HeightField field;
  field.size = {float(cells) * 2, float(cells) * 3};
  field.cellsX = cells;
  field.cellsZ = cells;
  field.biomeIds = {"grass", "stone", "sand"};
  field.biomeColors = {
      {0.2F, 0.7F, 0.1F, 1}, {0.4F, 0.4F, 0.4F, 1}, {0.8F, 0.6F, 0.3F, 1}};
  field.biomeTextureScales = {0.25F, 2.0F, 0.5F};
  const auto count = std::size_t(cells + 1) * std::size_t(cells + 1);
  field.heights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  for (int z = 0; z <= cells; ++z) {
    for (int x = 0; x <= cells; ++x) {
      const auto sample = field.index(x, z);
      field.heights.set(sample, float((x * 13 + z * 7) % 11) * 0.25F);
      // Distinct canonical normals reveal accidental Full recomputation.
      field.normals.set(sample, Vec3{0.1F, 0.95F, 0.2F});
      field.biomeIndices.set(sample, label(x, z));
    }
  }
  field.chunks.push_back({0, 0, cells, cells});
  return field;
}

using Triangle = std::array<float, 9>;

Triangle trianglePositions(const std::vector<Vec3> &vertices,
                           const std::vector<std::size_t> &indices,
                           std::size_t first) {
  Triangle result;
  for (std::size_t corner = 0; corner < 3; ++corner) {
    const auto &vertex = vertices.at(indices.at(first + corner));
    result[corner * 3] = vertex.x;
    result[corner * 3 + 1] = vertex.y;
    result[corner * 3 + 2] = vertex.z;
  }
  return result;
}

std::multiset<Triangle> triangles(const TerrainLodMesh &mesh) {
  std::multiset<Triangle> result;
  for (std::size_t first = 0; first < mesh.indices.size(); first += 3) {
    result.insert(trianglePositions(mesh.vertices, mesh.indices, first));
  }
  return result;
}

std::multiset<Triangle> triangles(const TerrainLodBiomeMesh &mesh) {
  std::multiset<Triangle> result;
  for (const auto &group : mesh.groups) {
    for (std::size_t first = 0; first < group.indices.size(); first += 3) {
      result.insert(trianglePositions(group.vertices, group.indices, first));
    }
  }
  return result;
}

void checkGroupData(const HeightField &field,
                    const TerrainLodBiomeGroup &group) {
  const auto found =
      std::find(field.biomeIds.begin(), field.biomeIds.end(), group.biomeId);
  assert(found != field.biomeIds.end());
  const auto biome = std::size_t(found - field.biomeIds.begin());
  assert(group.vertices.size() == group.normals.size());
  assert(group.vertices.size() == group.uvs.size());
  assert(group.vertices.size() == group.sampleIndices.size());
  assert(!group.indices.empty());
  assert(group.indices.size() % 3 == 0);
  for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex) {
    const auto &position = group.vertices[vertex];
    const auto expected = terrain_detail::terrainUv(
        {position.x, position.z}, field.biomeTextureScale(biome));
    assert(group.uvs[vertex].x == expected.x);
    assert(group.uvs[vertex].y == expected.y);
    assert(group.sampleIndices[vertex] < field.heights.size());
    assert(std::isfinite(group.normals[vertex].y));
  }
  for (const auto index : group.indices) {
    assert(index < group.vertices.size());
  }
}

void fullUsesPublishedTriangleOwnership() {
  const auto field =
      makeField(8, [](int x, int z) { return std::size_t((x + z * 2) % 3); });
  const auto full = buildTerrainLodBiomeMesh(field, field.chunks.front(),
                                             TerrainLod::Full, 0);
  assert(full);
  std::map<std::size_t, std::multiset<Triangle>> expected;
  for (int z = 0; z < field.cellsZ; ++z) {
    for (int x = 0; x < field.cellsX; ++x) {
      const auto a = field.index(x, z);
      const auto b = field.index(x + 1, z);
      const auto c = field.index(x, z + 1);
      const auto d = field.index(x + 1, z + 1);
      for (const std::array<std::size_t, 3> samples :
           {std::array{a, c, b}, std::array{b, c, d}}) {
        const auto first = field.biomeIndices.at(samples[0]);
        const auto second = field.biomeIndices.at(samples[1]);
        const auto third = field.biomeIndices.at(samples[2]);
        const auto owner = second == third ? second : first;
        Triangle positions;
        for (std::size_t corner = 0; corner < 3; ++corner) {
          const auto sample = samples[corner];
          const int sampleX = int(sample % std::size_t(field.cellsX + 1));
          const int sampleZ = int(sample / std::size_t(field.cellsX + 1));
          const auto point = field.position(sampleX, sampleZ);
          positions[corner * 3] = point.x;
          positions[corner * 3 + 1] = field.heights.at(sample);
          positions[corner * 3 + 2] = point.y;
        }
        expected[owner].insert(positions);
      }
    }
  }
  assert(full->groups.size() == expected.size());
  for (const auto &group : full->groups) {
    checkGroupData(field, group);
    const auto biome = std::size_t(
        std::find(field.biomeIds.begin(), field.biomeIds.end(), group.biomeId) -
        field.biomeIds.begin());
    std::multiset<Triangle> actual;
    for (std::size_t first = 0; first < group.indices.size(); first += 3) {
      actual.insert(trianglePositions(group.vertices, group.indices, first));
    }
    assert(actual == expected.at(biome));
    for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex) {
      const auto reference = field.normals.at(group.sampleIndices[vertex]);
      assert(group.normals[vertex].x == reference.x);
      assert(group.normals[vertex].y == reference.y);
      assert(group.normals[vertex].z == reference.z);
    }
  }
}

void reducedNeverIntroducesUnpublishedBiomes() {
  // A lone top-left sample is outvoted in its only Full triangle.
  const auto field = makeField(
      16, [](int x, int z) { return std::size_t(x == 0 && z == 0 ? 1 : 0); });
  for (const auto level : Levels) {
    const auto mesh =
        buildTerrainLodBiomeMesh(field, field.chunks.front(), level, 1);
    assert(mesh);
    assert(mesh->groups.size() == 1);
    assert(mesh->groups.front().biomeId == "grass");
  }
  assert(terrainLodBiomeForCell(field, 0, 0, 4) == 0);
}

void topologyAndMetadataSurvivePartitioning() {
  const auto field = makeField(23, [](int x, int z) {
    return std::size_t(z < 11 ? (x < 12 ? 0 : 1) : 2);
  });
  for (const TerrainChunk chunk :
       {TerrainChunk{0, 0, 23, 23}, TerrainChunk{7, 5, 13, 17},
        TerrainChunk{19, 19, 4, 4}, TerrainChunk{0, 0, 1, 3}}) {
    const auto full =
        buildTerrainLodBiomeMesh(field, chunk, TerrainLod::Full, 0);
    std::set<std::string> fullBiomes;
    for (const auto &group : full->groups) {
      fullBiomes.insert(group.biomeId);
    }
    for (const auto level : Levels) {
      for (const float skirtDepth : {0.0F, 1.25F}) {
        const auto flat = buildTerrainLodMesh(field, chunk, level, skirtDepth);
        const auto grouped =
            buildTerrainLodBiomeMesh(field, chunk, level, skirtDepth);
        const auto repeated =
            buildTerrainLodBiomeMesh(field, chunk, level, skirtDepth);
        assert(flat && grouped && repeated);
        assert(triangles(*flat) == triangles(*grouped));
        assert(grouped->skippedBiomes == 0);
        std::size_t coveredCells = 0;
        std::size_t skirtVertices = 0;
        std::size_t previousBiome = 0;
        for (std::size_t index = 0; index < grouped->groups.size(); ++index) {
          const auto &group = grouped->groups[index];
          assert(fullBiomes.contains(group.biomeId));
          const auto biome =
              std::size_t(std::find(field.biomeIds.begin(),
                                    field.biomeIds.end(), group.biomeId) -
                          field.biomeIds.begin());
          assert(index == 0 || biome > previousBiome);
          previousBiome = biome;
          checkGroupData(field, group);
          coveredCells += group.sourceCells;
          skirtVertices += group.skirtDepths;
          const auto &again = repeated->groups.at(index);
          assert(group.biomeId == again.biomeId);
          assert(group.indices == again.indices);
          assert(group.sampleIndices == again.sampleIndices);
          assert(group.sourceCells == again.sourceCells);
          assert(group.skirtDepths == again.skirtDepths);
        }
        assert(coveredCells ==
               std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ));
        assert(skirtVertices == flat->skirtDepths.size());
      }
    }
  }
}

template <class Operation> void refuses(Operation operation) {
  bool threw = false;
  try {
    operation();
  } catch (const std::invalid_argument &error) {
    assert(std::string(error.what()).size() > 0);
    threw = true;
  }
  assert(threw);
}

void invalidAndUnnamedFieldsRemainExplicit() {
  auto field = makeField(8, [](int, int) { return std::size_t(0); });
  assert(!buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Off,
                                   0));
  refuses([&] {
    (void)buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod(-1),
                                   0);
  });
  refuses([&] {
    (void)buildTerrainLodBiomeMesh(field, {7, 0, 2, 8}, TerrainLod::Half, 0);
  });
  refuses([&] { (void)terrainLodBiomeForCell(field, 0, 0, 0); });
  refuses([&] { (void)terrainLodBiomeForCell(field, 8, 0, 1); });
  auto unnamed = makeField(8, [](int, int) { return std::size_t(9); });
  const auto mesh = buildTerrainLodBiomeMesh(unnamed, unnamed.chunks.front(),
                                             TerrainLod::Quarter, 1);
  assert(mesh && mesh->empty());
  assert(mesh->skippedBiomes == 64);
  refuses([&] { (void)terrainLodBiomeForCell(unnamed, 0, 0, 2); });
  auto nonfinite = field;
  nonfinite.size.x = std::numeric_limits<float>::quiet_NaN();
  refuses([&] {
    (void)buildTerrainLodBiomeMesh(nonfinite, nonfinite.chunks.front(),
                                   TerrainLod::Full, 0);
  });
  nonfinite.size.x = field.size.x;
  nonfinite.size.y = std::numeric_limits<float>::infinity();
  refuses([&] { (void)terrainLodBiomeForCell(nonfinite, 0, 0, 1); });
  field.biomeIds.clear();
  refuses([&] {
    (void)buildTerrainLodBiomeMesh(field, field.chunks.front(),
                                   TerrainLod::Full, 0);
  });
}

} // namespace

int main() {
  fullUsesPublishedTriangleOwnership();
  reducedNeverIntroducesUnpublishedBiomes();
  topologyAndMetadataSurvivePartitioning();
  invalidAndUnnamedFieldsRemainExplicit();
  std::cout << "Terrain LOD biome partition checks passed\n";
}
