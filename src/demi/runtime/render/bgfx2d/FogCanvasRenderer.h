#pragma once
#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/visibility/FogOfWar2D.h"
#include <array>

namespace demi::runtime::render {
class FogCanvasRenderer {
public:
  explicit FogCanvasRenderer(GpuResources &resources) : resources_(resources) {}
  ~FogCanvasRenderer() { clear(); }
  void clear();
  bool draw(Canvas2D &canvas, const FogOfWar2D &fog,
            const Camera2DComponent &camera, Vec2 cameraPosition,
            std::uint16_t viewportWidth, std::uint16_t viewportHeight);

private:
  bool upload(const FogOfWar2D &fog);
  GpuResources &resources_;
  TextureHandle texture_;
  ProgramHandle program_;
  std::array<UniformHandle, 3> handles_;
  std::array<std::array<float, 4>, 3> values_;
  std::array<DrawUniformValue, 3> uniforms_;
  int width_ = 0;
  int height_ = 0;
  std::uint64_t revision_ = 0;
  std::uint64_t discoveryRevision_ = 0;
};
} // namespace demi::runtime::render
