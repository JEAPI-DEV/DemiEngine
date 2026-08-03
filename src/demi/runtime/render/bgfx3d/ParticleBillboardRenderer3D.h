#pragma once

#include "demi/runtime/render/ParticleSimulation3D.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

#include <cstdint>
#include <span>
#include <string>

namespace demi::runtime::render {

struct ParticleBillboardDraw3D {
  ParticleRenderData3D particle;
  TextureHandle texture;
  BlendMode blend = BlendMode::Alpha;
};

struct ParticleBillboardStatistics3D {
  std::uint32_t particles = 0;
  std::uint32_t drawCalls = 0;
  std::uint32_t triangles = 0;
};

class ParticleBillboardRenderer3D {
public:
  ParticleBillboardRenderer3D(GpuResources &resources,
                              RenderCommands &commands);
  ~ParticleBillboardRenderer3D();

  ParticleBillboardRenderer3D(const ParticleBillboardRenderer3D &) = delete;
  ParticleBillboardRenderer3D &
  operator=(const ParticleBillboardRenderer3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool draw(std::uint16_t viewId, const BgfxCameraFrame3D &camera,
                          std::span<const ParticleBillboardDraw3D> particles,
                          std::string &error);
  [[nodiscard]] const ParticleBillboardStatistics3D &statistics() const {
    return statistics_;
  }

private:
  GpuResources &resources_;
  RenderCommands &commands_;
  ProgramHandle program_;
  SamplerHandle sampler_;
  ParticleBillboardStatistics3D statistics_;
};

} // namespace demi::runtime::render
