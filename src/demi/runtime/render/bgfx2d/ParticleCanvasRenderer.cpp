#include "demi/runtime/render/bgfx2d/ParticleCanvasRenderer.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <algorithm>
#include <vector>

namespace demi::runtime::render {

ParticleCanvasRenderer::ParticleCanvasRenderer(Canvas2D &canvas,
                                               const TextureLibrary2D &textures)
    : canvas_(canvas), textures_(textures) {}

bool ParticleCanvasRenderer::draw(
    const std::span<const ParticleRenderData2D> particles,
    const Camera2DComponent &camera, const Vec2 cameraPosition,
    const std::uint16_t viewportWidth, const std::uint16_t viewportHeight) {
  const float ppu =
      viewportHeight / std::max(camera.orthographicSize * 2.0F, 1.0F);
  std::vector<const ParticleRenderData2D *> ordered;
  ordered.reserve(particles.size());
  for (const ParticleRenderData2D &particle : particles)
    if (particle.size > 0.0F)
      ordered.push_back(&particle);
  std::ranges::stable_sort(ordered, [](const auto *left, const auto *right) {
    return left->sortingOrder < right->sortingOrder;
  });

  for (const ParticleRenderData2D *particle : ordered) {
    const float screenX =
        viewportWidth * 0.5F + (particle->position.x - cameraPosition.x) * ppu;
    const float screenY =
        viewportHeight * 0.5F - (particle->position.y - cameraPosition.y) * ppu;
    const float size = particle->size * ppu;
    const std::uint32_t color = packVertexColorRgba8(particle->color);
    const TextureView2D texture = textures_.find(particle->texture);
    if (texture.handle) {
      if (!canvas_.imageTransformed(texture.handle, screenX, screenY, size,
                                    size, 0.5F, 0.5F,
                                    -particle->rotationRadians, {}, color))
        return false;
    } else if (!canvas_.circle(screenX, screenY, size * 0.5F, color, 20)) {
      return false;
    }
  }
  return true;
}

} // namespace demi::runtime::render
