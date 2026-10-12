#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/EntityLookup.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace demi::runtime {
class TerrainLodResidency;
struct World;
} // namespace demi::runtime

namespace demi::runtime::render {

// Scheduled image targets retain their last terrain demand between refreshes.
// Descriptors are weak, and replay verifies current native source identity, so
// camera removal/scene replacement never keeps obsolete geometry requested.
class TerrainCameraDemand3D {
public:
  void beginPresentation();
  void beginCamera(
      const World &world, const BgfxCameraFrame3D &camera,
      TerrainLodResidency &residency,
      const std::function<void(const std::shared_ptr<const HeightField> &)>
          &gpuTouch = {});
  void record(const BgfxCameraFrame3D &camera, const std::string &surfaceId,
              const std::string &owner,
              const std::shared_ptr<const PreparedTerrainChunkLod> &descriptor,
              std::size_t level, bool gpuField = false);
  void endPresentation();
  void clear();

private:
  struct SourceKey {
    std::string owner;
    const PreparedTerrainChunkLod *chunk = nullptr;
    bool operator==(const SourceKey &) const = default;
  };
  struct SourceHash {
    std::size_t operator()(const SourceKey &key) const;
  };
  struct Demand {
    std::string surfaceId;
    std::weak_ptr<const PreparedTerrainChunkLod> descriptor;
    std::size_t level = 0;
    bool gpuField = false;
  };
  struct CameraDemand {
    const World *world = nullptr;
    std::string worldId;
    std::string sceneId;
    bool visited = false;
    std::unordered_map<SourceKey, Demand, SourceHash> sources;
  };
  static std::string cameraKey(const BgfxCameraFrame3D &camera);
  std::unordered_map<std::string, CameraDemand> cameras_;
  EntityLookup lookup_;
};

} // namespace demi::runtime::render
