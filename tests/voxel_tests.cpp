#include "demi/runtime/voxel/VoxelBlockSet.h"
#include "demi/runtime/voxel/VoxelChunk.h"
#include "demi/runtime/voxel/VoxelMesher.h"
#include "demi/runtime/voxel/VoxelTerrain.h"
#include "demi/runtime/scene/VoxelChunkBuilder.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

demi::runtime::VoxelBlockSet testBlockSet() {
  demi::runtime::VoxelBlockSet blockSet;
  blockSet.blocks.push_back(demi::runtime::VoxelBlockDefinition{.id = 0, .name = "air", .solid = false});
  blockSet.indexById[0] = 0;
  blockSet.idByName["air"] = 0;
  blockSet.blocks.push_back(demi::runtime::VoxelBlockDefinition{.id = 1, .name = "stone", .solid = true, .tiles = {0, 0, 0, 0, 0, 0}});
  blockSet.indexById[1] = 1;
  blockSet.idByName["stone"] = 1;
  return blockSet;
}

bool expectFaces(const char* name, const demi::runtime::VoxelChunk& chunk, const int expectedFaces) {
  const demi::runtime::VoxelBlockSet blockSet = testBlockSet();
  const demi::runtime::VoxelMeshData mesh = demi::runtime::buildVoxelMesh(chunk, blockSet, demi::runtime::VoxelMeshBuildOptions{.atlasColumns = 1, .atlasRows = 1});
  if (mesh.faceCount != expectedFaces) {
    std::cerr << name << " expected " << expectedFaces << " visible face(s), got " << mesh.faceCount << ".\n";
    return false;
  }
  if (static_cast<int>(mesh.positions.size()) != expectedFaces * 6) {
    std::cerr << name << " emitted an unexpected vertex count.\n";
    return false;
  }
  if (mesh.positions.size() != mesh.normals.size() || mesh.positions.size() != mesh.texcoords.size()) {
    std::cerr << name << " emitted mismatched mesh attribute counts.\n";
    return false;
  }
  return true;
}

bool writeFile(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << contents;
  return true;
}

} // namespace

int main() {
  bool passed = true;

  demi::runtime::VoxelChunk empty(16, 16, 16);
  passed = expectFaces("empty chunk", empty, 0) && passed;

  demi::runtime::VoxelChunk single(16, 16, 16);
  single.setBlock(1, 1, 1, 1);
  passed = expectFaces("single block", single, 6) && passed;

  demi::runtime::VoxelChunk adjacent(16, 16, 16);
  adjacent.setBlock(1, 1, 1, 1);
  adjacent.setBlock(2, 1, 1, 1);
  passed = expectFaces("adjacent blocks", adjacent, 10) && passed;

  demi::runtime::VoxelChunk edge(1, 1, 1);
  edge.setBlock(0, 0, 0, 1);
  const demi::runtime::VoxelBlockSet edgeBlockSet = testBlockSet();
  const demi::runtime::VoxelMeshData edgeMesh = demi::runtime::buildVoxelMesh(
    edge,
    edgeBlockSet,
    demi::runtime::VoxelMeshBuildOptions{
      .atlasColumns = 1,
      .atlasRows = 1,
      .isSolidNeighbor = [](const int x, const int y, const int z) {
        return x == 1 && y == 0 && z == 0;
      },
    });
  if (edgeMesh.faceCount != 5) {
    std::cerr << "chunk edge meshing did not hide a face against a solid neighbor chunk.\n";
    passed = false;
  }

  demi::runtime::VoxelChunk full(2, 2, 2);
  full.fill(1);
  passed = expectFaces("full 2x2x2 chunk", full, 24) && passed;

  const std::filesystem::path blockSetPath = std::filesystem::temp_directory_path() / "demi_voxel_tests" / "tinted.voxel.json";
  if (!writeFile(blockSetPath, R"json({
    "format_version": 1,
    "tile_size": 16,
    "atlas_sources": [
      "plain.png",
      { "path": "tinted.png", "tint": [0.25, 0.50, 0.75, 1.0] }
    ],
    "blocks": [
      { "id": 0, "name": "air", "solid": false, "tile": 0 },
      { "id": 1, "name": "grass", "solid": true, "tiles": { "top": 1, "side": 0, "bottom": 0 } }
    ]
  })json")) {
    std::cerr << "failed to write voxel block set parser fixture.\n";
    return 1;
  }
  std::string blockSetError;
  const std::optional<demi::runtime::VoxelBlockSet> parsedBlockSet = demi::runtime::loadVoxelBlockSet(blockSetPath, "asset://test/tinted", blockSetError);
  if (!parsedBlockSet.has_value() || parsedBlockSet->atlasSources.size() != 2 || parsedBlockSet->atlasSources[1].tintG != 0.50F ||
      parsedBlockSet->tileForFace(1, demi::runtime::VoxelFace::Top) != 1) {
    std::cerr << "voxel block set did not parse atlas source tint or per-face tiles: " << blockSetError << '\n';
    passed = false;
  }

  demi::runtime::VoxelTerrainSettings terrain;
  terrain.seed = 42;
  terrain.baseHeight = 4;
  terrain.amplitude = 3;
  terrain.frequency = 0.12F;
  const int firstHeight = demi::runtime::voxelTerrainHeight(terrain, 10, 20, 16);
  const int secondHeight = demi::runtime::voxelTerrainHeight(terrain, 10, 20, 16);
  if (firstHeight != secondHeight) {
    std::cerr << "terrain generation was not deterministic for the same seed and coordinate.\n";
    passed = false;
  }

  demi::runtime::VoxelBlockSet blockSet = testBlockSet();
  blockSet.blocks.push_back(demi::runtime::VoxelBlockDefinition{.id = 2, .name = "grass", .solid = true, .tiles = {0, 0, 0, 0, 0, 0}});
  blockSet.indexById[2] = 2;
  blockSet.idByName["grass"] = 2;
  blockSet.blocks.push_back(demi::runtime::VoxelBlockDefinition{.id = 3, .name = "dirt", .solid = true, .tiles = {0, 0, 0, 0, 0, 0}});
  blockSet.indexById[3] = 3;
  blockSet.idByName["dirt"] = 3;
  demi::runtime::VoxelChunk terrainChunkA(16, 16, 16);
  demi::runtime::VoxelChunk terrainChunkB(16, 16, 16);
  demi::runtime::generateVoxelTerrain(terrainChunkA, blockSet, terrain, 0, 0);
  demi::runtime::generateVoxelTerrain(terrainChunkB, blockSet, terrain, 0, 0);
  for (int y = 0; y < terrainChunkA.height(); ++y) {
    for (int z = 0; z < terrainChunkA.depth(); ++z) {
      for (int x = 0; x < terrainChunkA.width(); ++x) {
        if (terrainChunkA.block(x, y, z) != terrainChunkB.block(x, y, z)) {
          std::cerr << "terrain chunks generated with the same seed did not match.\n";
          return 1;
        }
      }
    }
  }

  demi::runtime::VoxelChunkComponent layeredComponent;
  layeredComponent.width = 4;
  layeredComponent.height = 4;
  layeredComponent.depth = 4;
  layeredComponent.layers.push_back(demi::runtime::VoxelLayer{.block = "stone", .fromY = 2, .toY = 1});
  const demi::runtime::VoxelChunk layeredChunk = demi::runtime::buildVoxelChunkFromComponent(layeredComponent, blockSet);
  if (layeredChunk.block(0, 0, 0) != 0 || layeredChunk.block(0, 1, 0) != 1 || layeredChunk.block(0, 2, 0) != 1 ||
      layeredChunk.block(0, 3, 0) != 0) {
    std::cerr << "voxel chunk component builder did not apply clamped layer ranges.\n";
    passed = false;
  }

  demi::runtime::VoxelChunkComponent terrainComponent;
  terrainComponent.width = 16;
  terrainComponent.height = 16;
  terrainComponent.depth = 16;
  terrainComponent.terrain = terrain;
  terrainComponent.terrain.enabled = true;
  const demi::runtime::VoxelChunk componentTerrainA = demi::runtime::buildVoxelChunkFromComponent(terrainComponent, blockSet, demi::runtime::Vec3{16.0F, 0.0F, 0.0F});
  const demi::runtime::VoxelChunk componentTerrainB = demi::runtime::buildVoxelChunkFromComponent(terrainComponent, blockSet, demi::runtime::Vec3{16.0F, 0.0F, 0.0F});
  if (componentTerrainA.block(4, firstHeight, 4) != componentTerrainB.block(4, firstHeight, 4)) {
    std::cerr << "voxel chunk component builder did not produce stable terrain for the same origin.\n";
    passed = false;
  }

  return passed ? 0 : 1;
}
