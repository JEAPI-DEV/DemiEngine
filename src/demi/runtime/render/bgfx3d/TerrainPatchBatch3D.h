#pragma once

#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainPatchTopology3D.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime::render {
class TerrainCameraDemand3D;

struct TerrainPatchInstance3D {
  std::array<float, 16> transform{};
  // XY: lattice origin, Z: surface blending, W: reserved. This stays per
  // instance so shared fields can use different appearance without new uploads.
  std::array<float, 4> gridOrigin{0, 0, 1, 0};
};
static_assert(sizeof(TerrainPatchInstance3D) == 80);

struct TerrainPatchStatistics3D {
  std::size_t consideredPatches = 0;
  std::size_t culledPatches = 0;
  std::size_t draws = 0;
  std::size_t instances = 0;
  std::size_t triangles = 0;
  std::size_t fallbackPatches = 0;
  std::array<std::size_t, 4> selectedLevels{};
  std::string fallbackReason;
};

// A view-local patch queue over a persistent shared topology cache. Compatible
// owners are suppressed from ordinary biome extraction only after every visible
// chunk has a patch or an explicit Full fallback. Partial submission returns
// exactly the unsubmitted chunks' native mesh groups.
class TerrainPatchBatch3D {
public:
  explicit TerrainPatchBatch3D(GpuResources &resources);
  ~TerrainPatchBatch3D();
  TerrainPatchBatch3D(const TerrainPatchBatch3D &) = delete;
  TerrainPatchBatch3D &operator=(const TerrainPatchBatch3D &) = delete;

  void beginPresentation();
  void beginView();
  void clear();
  void prepareExtraction(const World &world, const BgfxCameraFrame3D &view,
                         const BgfxCameraFrame3D &lodCamera,
                         TerrainDrawSelection3D &selection,
                         TerrainGpuFieldCache3D &fields,
                         const MaterialLibrary &materials,
                         TerrainCameraDemand3D &demand,
                         std::size_t gpuBudgetBytes, bool programsReady);
  [[nodiscard]] bool suppressesOwner(const std::string &owner) const;
  [[nodiscard]] bool forcesFullOwner(const std::string &owner) const;

  // Explicit addition is also the small test seam for resource ownership,
  // grouping, and transient capacity handling without loading a World.
  bool addPatch(std::shared_ptr<const TerrainGpuField3D> field,
                TerrainPatchTopologyKey3D topology,
                const TerrainPatchInstance3D &instance,
                std::vector<VisibleMesh3D> fallbackMeshes,
                std::size_t gpuBudgetBytes, std::string &error);
  [[nodiscard]] std::vector<VisibleMesh3D>
  flush(RenderCommands &commands, std::uint16_t viewId, ProgramHandle program,
        const std::array<DrawUniformValue, 8> &lighting, bool overdraw,
        std::string &error);
  [[nodiscard]] const TerrainPatchStatistics3D &statistics() const;
  [[nodiscard]] std::size_t topologyBytes() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime::render
