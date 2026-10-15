#pragma once

#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayCache3D.h"

#include <functional>
#include <map>

namespace demi::runtime::render {

// Stable shader encoding independent of native enum ordinals.
[[nodiscard]] constexpr std::uint32_t
terrainMaterialWrapCode3D(TextureWrap wrap) {
  switch (wrap) {
  case TextureWrap::Repeat:
    return 0;
  case TextureWrap::Clamp:
    return 1;
  case TextureWrap::Mirror:
    return 2;
  }
  return 0;
}

// Chooses a material quality tier, never an authored source-image limit.
// Returns zero if even the smallest complete array bundle cannot fit.
[[nodiscard]] std::uint16_t terrainMaterialQualityEdge3D(
    const TerrainMaterialArraySource3D &source, std::uint32_t maximumEdge,
    std::uint32_t deviceMaximum, std::size_t gpuBytes, float detailDistance,
    float nearestVisibleDistance);

// Presentation-thread owner of immutable material snapshots and camera demand.
// Array packing and GPU lifetime remain owned by TerrainMaterialArrayCache3D.
class TerrainMaterialResidency3D {
public:
  using SourceFactory =
      std::function<std::shared_ptr<const TerrainMaterialArraySource3D>(
          const AssetRegistry &, const std::vector<std::string> &,
          std::string &)>;
  TerrainMaterialResidency3D(GpuResources &resources, JobSystem &jobs,
                             SourceFactory sourceFactory = {});
  void loadAssets(const AssetRegistry &registry);
  void setQuality(std::uint32_t maximumEdge, std::size_t gpuBytes,
                  float detailDistance, std::size_t stagingBytes,
                  std::size_t preparationJobs);
  void beginPresentation();
  void endPresentation();
  void clear();
  [[nodiscard]] TerrainMaterialArrayRequest3D
  request(const TerrainGpuField3D &field, float nearestVisibleChunkDistance);
  [[nodiscard]] bool hasPendingWork() const;
  [[nodiscard]] std::size_t residentBytes() const;
  [[nodiscard]] std::size_t pendingBytes() const;
  [[nodiscard]] std::size_t pendingJobs() const;
  [[nodiscard]] std::size_t uploadBytes() const;

private:
  struct SourceEntry {
    std::shared_ptr<const TerrainMaterialArraySource3D> source;
    std::string error;
    std::uint16_t currentDemand = 0;
    std::uint16_t previousDemand = 0;
  };
  GpuResources &resources_;
  TerrainMaterialArrayCache3D arrays_;
  SourceFactory sourceFactory_;
  AssetRegistry registry_;
  std::map<std::vector<std::string>, SourceEntry> sources_;
  std::uint32_t maximumEdge_ = 1024;
  std::size_t gpuBytes_ = 128U * 1024U * 1024U;
  float detailDistance_ = 200;
  std::string qualityError_;
};

} // namespace demi::runtime::render
