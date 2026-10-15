#pragma once

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuFieldData3D.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <array>
#include <memory>
#include <string>

namespace demi::runtime::render {

struct TerrainGpuTexture3D;

// A ready immutable binding set. Texture ownership follows this value through
// camera submission; replacing or evicting a cache entry cannot invalidate it.
struct TerrainGpuField3D {
  std::shared_ptr<const HeightField> source;
  TextureHandle height;
  TextureHandle normal;
  TextureHandle classification;
  TextureHandle palette;
  std::array<float, 4> fieldParameters{};
  std::array<float, 4> paletteParameters{};
  DrawState state;
  bool mapped = false;
  std::vector<std::string> materialIds;

private:
  friend class TerrainGpuFieldCache3D;
  std::array<std::shared_ptr<TerrainGpuTexture3D>, 4> textures_;
  std::shared_ptr<const std::vector<std::array<std::uint8_t, 4>>> ids_;
};

struct TerrainGpuFieldRequest3D {
  std::shared_ptr<const TerrainGpuField3D> field;
  bool preparing = false;
  std::string fallbackReason;
};

class TerrainGpuFieldCache3D {
public:
  TerrainGpuFieldCache3D(GpuResources &resources, JobSystem &jobs);
  ~TerrainGpuFieldCache3D();
  TerrainGpuFieldCache3D(const TerrainGpuFieldCache3D &) = delete;
  TerrainGpuFieldCache3D &operator=(const TerrainGpuFieldCache3D &) = delete;

  void setBudgets(std::size_t gpuBytes, std::size_t stagingBytes,
                  std::size_t preparationJobs);
  void beginPresentation();
  void endPresentation();
  void clear();
  void invalidateMaterials();
  void pollCompleted();
  void touch(const std::shared_ptr<const HeightField> &field);
  [[nodiscard]] TerrainGpuFieldRequest3D
  request(std::shared_ptr<const HeightField> field,
          const MaterialLibrary &materials);
  [[nodiscard]] TerrainGpuFieldRequest3D
  request(std::shared_ptr<const HeightField> field,
          const TerrainGpuMaterialResolver3D &resolveMaterial);
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
