#pragma once

#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/voxel/VoxelBlockSet.h"
#include "demi/runtime/voxel/VoxelChunk.h"

namespace demi::runtime {

[[nodiscard]] VoxelChunk buildVoxelChunkFromComponent(const VoxelChunkComponent& component,
                                                      const VoxelBlockSet& blockSet,
                                                      Vec3 worldOrigin = {});

} // namespace demi::runtime
