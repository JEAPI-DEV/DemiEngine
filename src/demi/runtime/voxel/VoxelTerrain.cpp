#include "demi/runtime/voxel/VoxelTerrain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace demi::runtime {

namespace {

float smoothstep(const float value) {
  return value * value * (3.0F - 2.0F * value);
}

std::uint32_t hash2D(const std::uint32_t seed, const int x, const int z) {
  std::uint32_t hash = seed;
  hash ^= static_cast<std::uint32_t>(x) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
  hash ^= static_cast<std::uint32_t>(z) + 0x85ebca6bU + (hash << 6U) + (hash >> 2U);
  hash ^= hash >> 16U;
  hash *= 0x7feb352dU;
  hash ^= hash >> 15U;
  hash *= 0x846ca68bU;
  hash ^= hash >> 16U;
  return hash;
}

float randomCorner(const std::uint32_t seed, const int x, const int z) {
  return static_cast<float>(hash2D(seed, x, z) & 0xffffU) / 65535.0F;
}

float valueNoise(const VoxelTerrainSettings& settings, const int worldX, const int worldZ) {
  const float sampleX = static_cast<float>(worldX) * std::max(settings.frequency, 0.0001F);
  const float sampleZ = static_cast<float>(worldZ) * std::max(settings.frequency, 0.0001F);
  const int x0 = static_cast<int>(std::floor(sampleX));
  const int z0 = static_cast<int>(std::floor(sampleZ));
  const int x1 = x0 + 1;
  const int z1 = z0 + 1;
  const float tx = smoothstep(sampleX - static_cast<float>(x0));
  const float tz = smoothstep(sampleZ - static_cast<float>(z0));

  const float a = randomCorner(settings.seed, x0, z0);
  const float b = randomCorner(settings.seed, x1, z0);
  const float c = randomCorner(settings.seed, x0, z1);
  const float d = randomCorner(settings.seed, x1, z1);
  const float top = a + (b - a) * tx;
  const float bottom = c + (d - c) * tx;
  return (top + (bottom - top) * tz) * 2.0F - 1.0F;
}

} // namespace

int voxelTerrainHeight(const VoxelTerrainSettings& settings, const int worldX, const int worldZ, const int maxHeight) {
  const int height = settings.baseHeight + static_cast<int>(std::round(valueNoise(settings, worldX, worldZ) * static_cast<float>(std::max(settings.amplitude, 0))));
  return std::clamp(height, 0, std::max(maxHeight - 1, 0));
}

void generateVoxelTerrain(VoxelChunk& chunk, const VoxelBlockSet& blockSet, const VoxelTerrainSettings& settings, const int originX, const int originZ) {
  const std::uint16_t surface = blockSet.blockIdByName(settings.surfaceBlock);
  const std::uint16_t subsurface = blockSet.blockIdByName(settings.subsurfaceBlock);
  const std::uint16_t stone = blockSet.blockIdByName(settings.stoneBlock);
  for (int z = 0; z < chunk.depth(); ++z) {
    for (int x = 0; x < chunk.width(); ++x) {
      const int height = voxelTerrainHeight(settings, originX + x, originZ + z, chunk.height());
      for (int y = 0; y <= height; ++y) {
        std::uint16_t block = stone;
        if (y == height) {
          block = surface;
        } else if (y >= height - std::max(settings.dirtDepth, 1)) {
          block = subsurface;
        }
        chunk.setBlock(x, y, z, block);
      }
    }
  }
}

} // namespace demi::runtime
