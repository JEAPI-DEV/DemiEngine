#pragma once

#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace demi::runtime {
struct MeshRendererComponent;
}

namespace demi::runtime::render {

// Terrain GPU residency is independent of Full CPU mesh/collision residency.
// Demand is marked by every camera/shadow in a presentation and evicted only
// after a short grace period. Failed uploads never destroy an existing entry.
class TerrainGpuMeshCache3D {
public:
  explicit TerrainGpuMeshCache3D(GpuResources &resources)
      : resources_(resources) {}
  void beginPresentation();
  void endPresentation(bool succeeded);
  void clear();
  const GpuMesh3D *
  get(const std::string &surfaceId, std::size_t level,
      const MeshRendererComponent &geometry,
      std::shared_ptr<const MeshRendererComponent> retainedGeometry,
      const MeshMaterialSections3D *sections, std::string &error);
  [[nodiscard]] std::size_t residentMeshes() const;
  [[nodiscard]] std::size_t residentBytes() const;
  [[nodiscard]] std::uint64_t uploads() const { return uploads_; }

private:
  struct Entry {
    explicit Entry(GpuResources &resources) : mesh(resources) {}
    GpuMesh3D mesh;
    std::weak_ptr<const MeshRendererComponent> geometry;
    std::uint64_t geometryRevision = 0;
    std::uint64_t indexRevision = 0;
    std::uint64_t lastPresentation = 0;
    std::size_t bytes = 0;
  };
  GpuResources &resources_;
  struct VariantKey {
    const MeshRendererComponent *geometry = nullptr;
    std::uint64_t indexRevision = 0;
    bool operator==(const VariantKey &) const = default;
  };
  struct VariantHash {
    std::size_t operator()(const VariantKey &key) const;
  };
  std::unordered_map<std::string, std::unique_ptr<Entry>> fullSurfaces_;
  // Immutable geometry is shared across placements; material bindings and tint
  // remain per draw. Weak source identity detects pointer reuse without keeping
  // evicted CPU geometry alive solely because its GPU buffers still exist.
  std::unordered_map<VariantKey, std::unique_ptr<Entry>, VariantHash> variants_;
  std::uint64_t presentation_ = 0;
  std::uint64_t uploads_ = 0;
};

} // namespace demi::runtime::render
