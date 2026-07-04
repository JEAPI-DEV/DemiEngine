#pragma once

#include "demi/runtime/voxel/VoxelBlockSet.h"
#include "demi/runtime/voxel/VoxelChunk.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

struct VoxelTerrainSettings {
  bool enabled = false;
  std::uint32_t seed = 1337;
  int baseHeight = 4;
  int amplitude = 3;
  float frequency = 0.08F;
  int dirtDepth = 3;
  std::string surfaceBlock = "grass";
  std::string subsurfaceBlock = "dirt";
  std::string stoneBlock = "stone";
};

[[nodiscard]] int voxelTerrainHeight(const VoxelTerrainSettings& settings, int worldX, int worldZ, int maxHeight);
void generateVoxelTerrain(VoxelChunk& chunk, const VoxelBlockSet& blockSet, const VoxelTerrainSettings& settings, int originX, int originZ);

} // namespace demi::runtime
