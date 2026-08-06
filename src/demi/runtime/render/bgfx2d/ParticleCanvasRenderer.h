#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/ParticleRenderData2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"

#include <cstdint>
#include <span>

namespace demi::runtime::render {

class ParticleCanvasRenderer {
public:
  ParticleCanvasRenderer(Canvas2D &canvas,
                         const TextureLibrary2D &textures);

  [[nodiscard]] bool draw(std::span<const ParticleRenderData2D> particles,
                          const Camera2DComponent &camera,
                          Vec2 cameraPosition, std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight);

private:
  Canvas2D &canvas_;
  const TextureLibrary2D &textures_;
};

} // namespace demi::runtime::render
