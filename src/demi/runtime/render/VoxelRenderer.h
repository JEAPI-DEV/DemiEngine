#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/voxel/VoxelBlockSet.h"

#include <raylib.h>

#include <string>
#include <unordered_map>

namespace demi::runtime {

class VoxelRenderer {
public:
  VoxelRenderer() = default;
  ~VoxelRenderer();

  VoxelRenderer(const VoxelRenderer&) = delete;
  VoxelRenderer& operator=(const VoxelRenderer&) = delete;

  void loadAssets(const AssetRegistry& registry);
  void drawWorld(const World& world);

private:
  struct LoadedBlockSet {
    VoxelBlockSet blockSet;
    Texture2D atlas{};
    int atlasColumns = 1;
    int atlasRows = 1;
  };

  struct CachedChunkMesh {
    std::string signature;
    Model model{};
    int faceCount = 0;
    bool hasModel = false;
  };

  struct RenderChunk;

  [[nodiscard]] CachedChunkMesh buildChunkMesh(const RenderChunk& chunk, const std::vector<RenderChunk>& chunks) const;

  std::unordered_map<std::string, LoadedBlockSet> blockSets_;
  std::unordered_map<std::string, CachedChunkMesh> chunkMeshes_;
};

} // namespace demi::runtime
