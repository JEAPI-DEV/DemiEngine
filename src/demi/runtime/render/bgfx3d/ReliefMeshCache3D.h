#pragma once
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/backend/ImageDecoder2D.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include <map>
#include <memory>
namespace demi::runtime::render {
// Session-owned procedural geometry: no generated model files or per-wall
// copies.
class ReliefMeshCache3D {
public:
  explicit ReliefMeshCache3D(GpuResources &resources) : resources_(resources) {}
  void loadAssets(const AssetRegistry &registry);
  void clear();
  void beginFrame() { ++frame_; }
  void setRetentionBudget(std::size_t meshes,std::size_t imageBytes) {
    retainedMeshes_=meshes;imageBytes_=imageBytes;
  }
  const GpuMesh3D *get(const SurfaceRelief3DComponent &relief, Vec3 size,
                       std::string &error);
  std::size_t size() const { return meshes_.size(); }

private:
  GpuResources &resources_;
  std::map<std::string, std::filesystem::path> sources_;
  std::map<std::string, ImageData2D> images_;
  struct Entry {
    std::unique_ptr<GpuMesh3D> mesh;
    std::uint64_t frame = 0;
  };
  std::map<std::string, Entry> meshes_;
  std::uint64_t frame_ = 0;
  std::size_t retainedMeshes_=256, imageBytes_=64U*1024U*1024U;
};
} // namespace demi::runtime::render
