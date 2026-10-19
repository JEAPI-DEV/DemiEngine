#pragma once

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayData3D.h"

namespace demi::runtime::render {

struct TerrainMaterialArrays3D {
  std::shared_ptr<const TerrainMaterialArraySource3D> source;
  std::array<TextureHandle, terrainMaterialArrayCount3D> textures{};
  std::uint16_t edge = 0;
  std::size_t residentBytes = 0;

private:
  friend class TerrainMaterialArrayCache3D;
  std::shared_ptr<void> ownership_;
};

struct TerrainMaterialArrayRequest3D {
  std::shared_ptr<const TerrainMaterialArrays3D> arrays;
  bool preparing = false;
  std::string fallbackReason;
  // A resident metadata snapshot is sufficient until a new tier needs pixels.
  bool needsSourceSnapshot = false;
};

// CPU preparation uses the renderer's existing worker pool; GPU creation and
// destruction remain on the owning presentation thread. Requests never wait.
class TerrainMaterialArrayCache3D {
public:
  TerrainMaterialArrayCache3D(GpuResources &resources, JobSystem &jobs);
  ~TerrainMaterialArrayCache3D();
  TerrainMaterialArrayCache3D(const TerrainMaterialArrayCache3D &) = delete;
  TerrainMaterialArrayCache3D &
  operator=(const TerrainMaterialArrayCache3D &) = delete;

  void setBudgets(std::size_t gpuBytes, std::size_t stagingBytes,
                  std::size_t preparationJobs);
  void beginPresentation();
  void endPresentation();
  void clear();
  void invalidate();
  [[nodiscard]] TerrainMaterialArrayRequest3D
  request(std::shared_ptr<const TerrainMaterialArraySource3D> source,
          std::uint16_t maximumEdge);
  [[nodiscard]] bool hasPendingWork() const;
  [[nodiscard]] std::size_t residentBytes() const;
  [[nodiscard]] std::size_t pendingBytes() const;
  [[nodiscard]] std::size_t pendingJobs() const;
  [[nodiscard]] std::size_t uploadBytes() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime::render
