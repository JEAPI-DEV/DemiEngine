#pragma once

#include "demi/runtime/render/bgfx3d/WaterSurfaceRenderer3D.h"
#include <map>

namespace demi::runtime::render {

// Per-camera snapshots stay immutable until all consumers have submitted.
// MSAA remains on the scene target; depth uses a separate sampling prepass.
class OpaqueSceneCapture3D {
public:
  struct Target {
    RenderTargetHandles depth;
    TextureHandle color;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool used = false;
    WaterSceneCapture3D sample(bool originBottomLeft) const;
  };

  explicit OpaqueSceneCapture3D(GpuResources &resources)
      : resources_(resources) {}
  ~OpaqueSceneCapture3D();
  OpaqueSceneCapture3D(const OpaqueSceneCapture3D &) = delete;
  OpaqueSceneCapture3D &operator=(const OpaqueSceneCapture3D &) = delete;
  void beginPresentation();
  void endPresentation();
  void clear();
  Target *prepare(std::uint16_t cameraView, std::uint16_t width,
                  std::uint16_t height, std::string &error);

private:
  void destroy(Target &target);
  GpuResources &resources_;
  std::map<std::uint16_t, Target> targets_;
};
} // namespace demi::runtime::render
