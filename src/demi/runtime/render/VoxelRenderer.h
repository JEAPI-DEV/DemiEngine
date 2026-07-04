#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/voxel/VoxelBlockSet.h"
#include "demi/runtime/voxel/VoxelChunk.h"

#include <raylib.h>

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct VoxelRendererStats {
  int visibleChunks = 0;
  int chunkDataBuilds = 0;
  int chunkMeshBuilds = 0;
  int uploadedFaces = 0;
  double chunkDataMs = 0.0;
  double meshBuildMs = 0.0;
  double totalMs = 0.0;
};

class VoxelRenderer {
public:
  VoxelRenderer() = default;
  ~VoxelRenderer();

  VoxelRenderer(const VoxelRenderer&) = delete;
  VoxelRenderer& operator=(const VoxelRenderer&) = delete;

  void loadAssets(const AssetRegistry& registry);
  void drawWorld(const World& world);
  [[nodiscard]] const VoxelRendererStats& stats() const;

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

  struct CachedChunkData {
    std::string signature;
    VoxelChunk data;
  };

  struct RenderChunk;

  [[nodiscard]] CachedChunkMesh buildChunkMesh(const RenderChunk& chunk, const std::vector<RenderChunk>& chunks) const;
  [[nodiscard]] static bool canAffectBoundaryMesh(const RenderChunk& chunk, const RenderChunk& neighbor);
  [[nodiscard]] static std::string chunkNeighborSignature(const RenderChunk& chunk, const std::vector<RenderChunk>& chunks);
  void unloadStaleChunks(const std::unordered_set<std::string>& visibleChunkIds);

  std::unordered_map<std::string, LoadedBlockSet> blockSets_;
  std::unordered_map<std::string, CachedChunkData> chunkData_;
  std::unordered_map<std::string, CachedChunkMesh> chunkMeshes_;
  VoxelRendererStats stats_;
};

} // namespace demi::runtime
