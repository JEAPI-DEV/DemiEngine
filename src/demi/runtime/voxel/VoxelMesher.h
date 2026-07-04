#pragma once

#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/voxel/VoxelBlockSet.h"
#include "demi/runtime/voxel/VoxelChunk.h"

#include <functional>
#include <vector>

namespace demi::runtime {

struct VoxelMeshBuildOptions {
  int atlasColumns = 1;
  int atlasRows = 1;
  std::function<bool(int x, int y, int z)> isSolidNeighbor;
};

struct VoxelMeshData {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> texcoords;
  int faceCount = 0;
};

[[nodiscard]] VoxelMeshData buildVoxelMesh(const VoxelChunk& chunk, const VoxelBlockSet& blockSet, VoxelMeshBuildOptions options);

} // namespace demi::runtime
