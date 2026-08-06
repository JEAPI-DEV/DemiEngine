#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/ParticleSimulation3D.h"
#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/render/bgfx3d/ParticleBillboardRenderer3D.h"
#include "demi/runtime/render/bgfx3d/PostProcessRenderer3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"
#include "demi/runtime/scene/model/World.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

// Runtime-facing composition root for bgfx 3D rendering. Specialized mesh,
// particle, post-process, camera, and overlay systems retain their own narrow
// ownership boundaries.
class BgfxRenderer3D {
public:
  BgfxRenderer3D(GpuResources &resources, RenderCommands &commands);
  ~BgfxRenderer3D();

  BgfxRenderer3D(const BgfxRenderer3D &) = delete;
  BgfxRenderer3D &operator=(const BgfxRenderer3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics);
  [[nodiscard]] bool renderFrame(const World &world,
                                 const BgfxCameraFrame3D &frame,
                                 float deltaSeconds, std::string &error);

  [[nodiscard]] const RenderStatistics &statistics() const {
    return statistics_;
  }

private:
  struct RenderTarget {
    RenderTargetHandles handles;
    std::uint16_t width = 1;
    std::uint16_t height = 1;
  };

  struct CachedMesh {
    explicit CachedMesh(GpuResources &resources) : gpu(resources) {}
    GpuMesh3D gpu;
    std::uint64_t signature = 0;
  };

  GpuResources &resources_;
  RenderCommands &commands_;
  PrimitiveCanvas3D primitives_;
  PostProcessRenderer3D postProcess_;
  ParticleBillboardRenderer3D particleRenderer_;
  BgfxRenderer2D overlay_;
  TextureLibrary2D textures_;
  MaterialLibrary materials_;
  ProgramHandle meshProgram_;
  ProgramHandle instancedMeshProgram_;
  SamplerHandle meshSampler_;
  UniformHandle tintUniform_;
  UniformHandle alphaCutoffUniform_;
  UniformHandle lightDirectionUniform_;
  UniformHandle lightColorUniform_;
  UniformHandle ambientColorUniform_;
  UniformHandle pointPositionRangeUniform_;
  UniformHandle pointColorIntensityUniform_;
  UniformHandle spotPositionRangeUniform_;
  UniformHandle spotDirectionOuterUniform_;
  UniformHandle spotColorIntensityUniform_;
  UniformHandle spotInnerUniform_;
  TextureHandle whiteTexture_;
  std::unordered_map<std::string, std::unique_ptr<CachedMesh>> dynamicMeshes_;
  std::unordered_map<std::string, std::unique_ptr<CachedMesh>> modelMeshes_;
  std::unordered_map<std::string, assets::GltfSkinnedModel3D> animatedModels_;
  std::unordered_map<std::string, std::string> modelTextures_;
  std::unordered_map<std::string, RenderTarget> renderTargets_;
  ParticleSimulation3D particles_;
  RenderStatistics statistics_;
  bool initialized_ = false;
};

} // namespace demi::runtime::render
