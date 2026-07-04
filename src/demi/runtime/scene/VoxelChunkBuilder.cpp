#include "demi/runtime/scene/VoxelChunkBuilder.h"

#include "demi/runtime/voxel/VoxelTerrain.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

VoxelChunk buildVoxelChunkFromComponent(const VoxelChunkComponent& component, const VoxelBlockSet& blockSet, const Vec3 worldOrigin) {
  VoxelChunk chunk(std::max(component.width, 1), std::max(component.height, 1), std::max(component.depth, 1));
  if (component.terrain.enabled) {
    generateVoxelTerrain(chunk, blockSet, component.terrain, static_cast<int>(std::floor(worldOrigin.x)), static_cast<int>(std::floor(worldOrigin.z)));
    return chunk;
  }

  for (const VoxelLayer& layer : component.layers) {
    const std::uint16_t blockId = blockSet.blockIdByName(layer.block);
    const int fromY = std::clamp(std::min(layer.fromY, layer.toY), 0, chunk.height() - 1);
    const int toY = std::clamp(std::max(layer.fromY, layer.toY), 0, chunk.height() - 1);
    for (int y = fromY; y <= toY; ++y) {
      for (int z = 0; z < chunk.depth(); ++z) {
        for (int x = 0; x < chunk.width(); ++x) {
          chunk.setBlock(x, y, z, blockId);
        }
      }
    }
  }

  return chunk;
}

} // namespace demi::runtime
