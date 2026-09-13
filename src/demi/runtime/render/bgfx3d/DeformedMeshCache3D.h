#pragma once

#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/geometry/MeshRefinement3D.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime::render {
// Shared rest geometry stays immutable. Only dented instances own private GPU
// buffers; unchanged dents require neither deformation work nor uploads.
class DeformedMeshCache3D {
public:
  explicit DeformedMeshCache3D(GpuResources &resources)
      : resources_(resources) {}
  [[nodiscard]] const GpuMesh3D *
  get(const std::string &entityId, const std::string &sourceId,
      std::uint64_t sourceRevision, std::span<const Vec3> positions,
      std::span<const Vec2> uvs, std::span<const std::uint32_t> indices,
      std::span<const std::uint32_t> colors, std::span<const MeshDent3D> dents,
      std::string &error);
  void retain(const std::unordered_set<std::string> &live);
  void clear() { meshes_.clear(); }
  [[nodiscard]] std::size_t size() const { return meshes_.size(); }
  [[nodiscard]] std::uint64_t uploads() const { return uploads_; }

private:
  struct Entry {
    explicit Entry(GpuResources &resources) : gpu(resources) {}
    GpuMesh3D gpu;
    std::string sourceId;
    std::uint64_t sourceRevision = 0;
    std::uint64_t dentSignature = 0;
  };
  GpuResources &resources_;
  std::unordered_map<std::string, std::unique_ptr<Entry>> meshes_;
  std::uint64_t uploads_ = 0;
};
} // namespace demi::runtime::render
