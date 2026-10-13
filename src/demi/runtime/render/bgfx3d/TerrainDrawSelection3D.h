#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace demi::runtime {
struct PreparedTerrainChunkLod;
}

namespace demi::runtime::render {

// Conservative screen-space geometric error against the originating camera.
// A whole chunk shares the decision across biome/material surface groups.
[[nodiscard]] std::size_t
selectTerrainDrawLevel3D(const PreparedTerrainChunkLod &chunk,
                         const WorldTransform3D &transform, Vec3 meshSize,
                         const BgfxCameraFrame3D &camera, float errorPixels);

// Per-camera threshold hysteresis: refine as soon as the budget is exceeded,
// coarsen only with 15 percent headroom. Camera decisions never share history.
class TerrainDrawSelection3D {
public:
  void beginPresentation();
  void clear();
  std::size_t
  select(const std::shared_ptr<const PreparedTerrainChunkLod> &chunk,
         const WorldTransform3D &transform, Vec3 meshSize,
         const BgfxCameraFrame3D &camera, float errorPixels,
         const std::string &owner = {});

private:
  struct State {
    std::weak_ptr<const PreparedTerrainChunkLod> chunk;
    std::size_t level = 0;
    std::uint64_t lastPresentation = 0;
  };
  using ChunkStates =
      std::unordered_map<const PreparedTerrainChunkLod *, State>;
  using OwnerStates = std::unordered_map<std::string, ChunkStates>;
  std::unordered_map<std::string, OwnerStates> cameras_;
  std::uint64_t presentation_ = 0;
};

} // namespace demi::runtime::render
