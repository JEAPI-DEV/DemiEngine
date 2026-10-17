#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace demi::runtime::render {

class GpuResources;
class RenderCommands;

// Immutable opaque snapshots. Depth.r is positive camera-forward eye depth
// divided by the camera far clip; untouched background is one. Neither texture
// may be an attachment of the currently bound water framebuffer.
struct WaterSceneCapture3D {
  TextureHandle color;
  TextureHandle linearDepth;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  bool originBottomLeft = false;
};

struct WaterSurfaceStatistics3D {
  std::size_t draws = 0;
  std::size_t triangles = 0;
};

// Draws actual prepared water coverage against the opaque snapshot. The caller
// owns pass ordering and binds the original MSAA depth attachment for testing.
class WaterSurfaceRenderer3D {
public:
  WaterSurfaceRenderer3D(GpuResources &resources, RenderCommands &commands);
  ~WaterSurfaceRenderer3D();
  WaterSurfaceRenderer3D(const WaterSurfaceRenderer3D &) = delete;
  WaterSurfaceRenderer3D &operator=(const WaterSurfaceRenderer3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void beginPresentation();
  void endPresentation();
  void clear();
  [[nodiscard]] bool draw(std::span<const VisibleMesh3D> surfaces,
                          const BgfxCameraFrame3D &frame,
                          const WaterSceneCapture3D &capture,
                          TextureHandle environmentPanorama,
                          const SceneLighting3D &lighting, float elapsedSeconds,
                          std::string &error);
  [[nodiscard]] const WaterSurfaceStatistics3D &statistics() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime::render
