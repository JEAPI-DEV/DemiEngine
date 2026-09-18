#pragma once

#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

namespace demi::runtime::render {

// Camera-centered, unlit equirectangular background. Owns only a reusable cube
// and shader; the normal asset service owns the panorama texture.
class SkyRenderer3D {
public:
  explicit SkyRenderer3D(GpuResources &resources)
      : resources_(resources), mesh_(resources) {}
  ~SkyRenderer3D() { clear(); }
  bool draw(RenderCommands &commands, std::uint16_t view, TextureHandle texture,
            Vec3 cameraPosition, std::string &error);
  void clear();

private:
  GpuResources &resources_;
  GpuMesh3D mesh_;
  ProgramHandle program_;
  SamplerHandle sampler_;
};

} // namespace demi::runtime::render
