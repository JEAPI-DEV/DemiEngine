#include "demi/runtime/render/bgfx3d/TerrainPatchTopology3D.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainLodGeometry.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

constexpr std::array Levels{TerrainLod::Full, TerrainLod::Half,
                            TerrainLod::Quarter, TerrainLod::Coarse};

bool samePosition(Vec3 first, Vec3 second) {
  return first.x == second.x && first.y == second.y && first.z == second.z;
}

void topologyIsIndexedUpwardAndCoversRectangle() {
  for (const auto dimensions :
       {std::array{16, 16}, std::array{7, 9}, std::array{5, 3},
        std::array{1, 7}, std::array{7, 1}, std::array{1, 1}}) {
    for (const auto level : Levels) {
      const auto mesh =
          createTerrainPatchTopology3D({dimensions[0], dimensions[1], level});
      assert(mesh.positions.size() == mesh.textureCoordinates.size());
      assert(
          mesh.indices.size() / 3 ==
          terrainLodSurfaceTriangleCount(dimensions[0], dimensions[1], level));
      double area = 0;
      for (std::size_t triangle = 0; triangle < mesh.indices.size();
           triangle += 3) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
          assert(mesh.indices[triangle + corner] < mesh.positions.size());
        }
        const auto a = mesh.positions[mesh.indices[triangle]];
        const auto b = mesh.positions[mesh.indices[triangle + 1]];
        const auto c = mesh.positions[mesh.indices[triangle + 2]];
        const double twiceArea =
            double(b.z - a.z) * (c.x - a.x) - double(b.x - a.x) * (c.z - a.z);
        assert(twiceArea > 0);
        area += twiceArea * 0.5;
      }
      assert(area == double(dimensions[0]) * dimensions[1]);
      for (std::size_t vertex = 0; vertex < mesh.positions.size(); ++vertex) {
        const auto position = mesh.positions[vertex];
        const auto uv = mesh.textureCoordinates[vertex];
        assert(position.y == 0 && position.x >= 0 && position.z >= 0);
        assert(position.x <= dimensions[0] && position.z <= dimensions[1]);
        assert(std::floor(position.x) == position.x &&
               std::floor(position.z) == position.z);
        assert(std::abs(uv.x - position.x / dimensions[0]) < 1e-6F);
        assert(std::abs(uv.y - position.z / dimensions[1]) < 1e-6F);
      }
    }
  }
}

std::size_t vertexAt(const PrimitiveMeshData3D &mesh, int x, int z) {
  const auto found = std::find_if(
      mesh.positions.begin(), mesh.positions.end(),
      [&](Vec3 position) { return position.x == x && position.z == z; });
  assert(found != mesh.positions.end());
  return std::size_t(found - mesh.positions.begin());
}

bool hasEdge(const PrimitiveMeshData3D &mesh, std::size_t first,
             std::size_t second) {
  for (std::size_t triangle = 0; triangle < mesh.indices.size();
       triangle += 3) {
    for (std::size_t edge = 0; edge < 3; ++edge) {
      const auto a = mesh.indices[triangle + edge];
      const auto b = mesh.indices[triangle + (edge + 1) % 3];
      if ((a == first && b == second) || (a == second && b == first)) {
        return true;
      }
    }
  }
  return false;
}

void everyFinePerimeterSegmentSurvivesEveryLevel() {
  constexpr int Width = 7;
  constexpr int Height = 9;
  for (const auto level : Levels) {
    const auto mesh = createTerrainPatchTopology3D({Width, Height, level});
    for (int x = 0; x < Width; ++x) {
      for (const int z : {0, Height}) {
        assert(hasEdge(mesh, vertexAt(mesh, x, z), vertexAt(mesh, x + 1, z)));
      }
    }
    for (int z = 0; z < Height; ++z) {
      for (const int x : {0, Width}) {
        assert(hasEdge(mesh, vertexAt(mesh, x, z), vertexAt(mesh, x, z + 1)));
      }
    }
  }
}

void equalKeysReuseIdenticalGeometryWithoutFieldOrigin() {
  const TerrainPatchTopologyKey3D key{7, 9, TerrainLod::Quarter};
  const auto first = createTerrainPatchTopology3D(key);
  const auto second = createTerrainPatchTopology3D(key);
  assert(first.indices == second.indices);
  assert(std::ranges::equal(first.positions, second.positions, samePosition));
  assert(std::ranges::equal(
      first.textureCoordinates, second.textureCoordinates,
      [](Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }));

  const TerrainChunk translated{31, 23, key.cellsX, key.cellsZ};
  const auto source =
      terrain_detail::buildTerrainLodTopology(translated, key.level);
  assert(source.indices.size() == first.indices.size());
  assert(std::ranges::equal(source.indices, first.indices));
  for (std::size_t vertex = 0; vertex < source.samples.size(); ++vertex) {
    assert(first.positions[vertex].x ==
           source.samples[vertex].cellX - translated.firstCellX);
    assert(first.positions[vertex].z ==
           source.samples[vertex].cellZ - translated.firstCellZ);
  }
  std::map<TerrainPatchTopologyKey3D, int> identities;
  identities.emplace(key, 1);
  assert(!identities.emplace(key, 2).second);
  assert(
      identities.emplace(TerrainPatchTopologyKey3D{9, 7, key.level}, 3).second);
  assert(
      identities.emplace(TerrainPatchTopologyKey3D{7, 9, TerrainLod::Full}, 4)
          .second);
}

void invalidAndUnaddressableRequestsFailBeforeAllocation() {
  for (const auto key :
       {TerrainPatchTopologyKey3D{0, 8, TerrainLod::Full},
        TerrainPatchTopologyKey3D{8, -1, TerrainLod::Full},
        TerrainPatchTopologyKey3D{8, 8, TerrainLod::Off},
        TerrainPatchTopologyKey3D{8, 8, static_cast<TerrainLod>(99)}}) {
    bool rejected = false;
    try {
      (void)createTerrainPatchTopology3D(key);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  bool rejected = false;
  try {
    (void)createTerrainPatchTopology3D({std::numeric_limits<int>::max(),
                                        std::numeric_limits<int>::max(),
                                        TerrainLod::Full});
  } catch (const std::length_error &) {
    rejected = true;
  }
  assert(rejected);
}

} // namespace

int main() {
  topologyIsIndexedUpwardAndCoversRectangle();
  everyFinePerimeterSegmentSurvivesEveryLevel();
  equalKeysReuseIdenticalGeometryWithoutFieldOrigin();
  invalidAndUnaddressableRequestsFailBeforeAllocation();
  std::cout << "Terrain patch topology checks passed\n";
}
