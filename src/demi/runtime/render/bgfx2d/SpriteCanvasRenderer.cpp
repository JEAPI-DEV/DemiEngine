#include "demi/runtime/render/bgfx2d/SpriteCanvasRenderer.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <tuple>
#include <vector>

namespace demi::runtime::render {

SpriteCanvasRenderer::SpriteCanvasRenderer(
    Canvas2D &canvas, const TextureLibrary2D &textures,
    const std::unordered_map<std::string, TextureAnimation2D> *animations)
    : canvas_(canvas), textures_(textures), animations_(animations) {}

bool SpriteCanvasRenderer::draw(const World &world,
                                const Camera2DComponent &camera,
                                const Vec2 cameraPosition,
                                const std::uint16_t viewportWidth,
                                const std::uint16_t viewportHeight,
                                const float animationTime) {
  const float ppu =
      viewportHeight / std::max(camera.orthographicSize * 2.0F, 1.0F);
  std::vector<const Entity *> sprites;
  for (const Entity &entity : world.entities)
    if (entity.enabled && entity.hasComponent<SpriteComponent>() &&
        !entity.hasComponent<IsoTransformComponent>())
      sprites.push_back(&entity);
  std::ranges::stable_sort(
      sprites, [](const Entity *left, const Entity *right) {
        const SpriteComponent &a = *left->component<SpriteComponent>();
        const SpriteComponent &b = *right->component<SpriteComponent>();
        return std::tuple{a.sortingOrder, a.layer, left->id} <
               std::tuple{b.sortingOrder, b.layer, right->id};
      });

  for (const Entity *entity : sprites) {
    const SpriteComponent &sprite = *entity->component<SpriteComponent>();
    const Vec2 position = worldPosition2D(world, *entity);
    const float screenX =
        viewportWidth * 0.5F + (position.x - cameraPosition.x) * ppu;
    const float screenY =
        viewportHeight * 0.5F - (position.y - cameraPosition.y) * ppu;
    const float width = (sprite.size.x > 0.0F ? sprite.size.x : 1.0F) * ppu;
    const float height = (sprite.size.y > 0.0F ? sprite.size.y : 1.0F) * ppu;
    const std::uint32_t color = packVertexColorRgba8(sprite.color);
    ScissorRect scissor;
    if (sprite.maskSize.x > 0.0F && sprite.maskSize.y > 0.0F) {
      const float maskWidth = sprite.maskSize.x * ppu;
      const float maskHeight = sprite.maskSize.y * ppu;
      scissor = {
          .x = static_cast<std::uint16_t>(std::max(
              screenX + sprite.maskOffset.x * ppu - maskWidth * 0.5F, 0.0F)),
          .y = static_cast<std::uint16_t>(std::max(
              screenY - sprite.maskOffset.y * ppu - maskHeight * 0.5F, 0.0F)),
          .width = static_cast<std::uint16_t>(std::max(maskWidth, 1.0F)),
          .height = static_cast<std::uint16_t>(std::max(maskHeight, 1.0F)),
      };
    }

    TextureView2D texture;
    if (const auto *animator = entity->component<SpriteAnimator2DComponent>()) {
      texture = textures_.find(sprite.texture + "#" +
                               std::to_string(animator->currentFrame));
    }
    if (!texture.handle && animations_ != nullptr) {
      if (const auto found = animations_->find(sprite.texture);
          found != animations_->end() && found->second.frameCount > 0) {
        const std::size_t frame =
            textureAnimationFrameAt(found->second, animationTime);
        texture = textures_.find(sprite.texture + "#" + std::to_string(frame));
      }
    }
    if (!texture.handle)
      texture = textures_.find(sprite.texture);
    if (!texture.handle) {
      if (sprite.shape == "circle") {
        if (!canvas_.circle(screenX, screenY, std::min(width, height) * 0.5F,
                            color, 32, BlendMode::Alpha, scissor))
          return false;
      } else if (sprite.shape == "triangle") {
        if (!canvas_.circle(screenX, screenY, std::min(width, height) * 0.5F,
                            color, 3, BlendMode::Alpha, scissor))
          return false;
      } else if (!canvas_.solid({.x = screenX - sprite.pivot.x * width,
                                 .y = screenY - sprite.pivot.y * height,
                                 .width = width,
                                 .height = height},
                                color, BlendMode::Alpha, scissor)) {
        return false;
      }
      continue;
    }

    float sourceX = sprite.sourcePosition.x;
    float sourceY = sprite.sourcePosition.y;
    float sourceWidth =
        sprite.sourceSize.x > 0.0F ? sprite.sourceSize.x : texture.width;
    float sourceHeight =
        sprite.sourceSize.y > 0.0F ? sprite.sourceSize.y : texture.height;
    if (sprite.sourceNormalized) {
      sourceX *= texture.width;
      sourceY *= texture.height;
      sourceWidth = sprite.sourceSize.x > 0.0F
                        ? sprite.sourceSize.x * texture.width
                        : texture.width;
      sourceHeight = sprite.sourceSize.y > 0.0F
                         ? sprite.sourceSize.y * texture.height
                         : texture.height;
    }
    if (const auto *animator = entity->component<SpriteAnimator2DComponent>();
        animator != nullptr && animator->frameSize.x > 0.0F &&
        animator->frameSize.y > 0.0F) {
      sourceWidth = animator->frameSize.x;
      sourceHeight = animator->frameSize.y;
      const int columns =
          std::max(static_cast<int>(texture.width / sourceWidth), 1);
      sourceX = (animator->currentFrame % columns) * sourceWidth;
      sourceY = (animator->currentFrame / columns) * sourceHeight;
    }
    TextureRegion2D source{
        .u0 = sourceX / texture.width,
        .v0 = sourceY / texture.height,
        .u1 = (sourceX + sourceWidth) / texture.width,
        .v1 = (sourceY + sourceHeight) / texture.height,
    };
    if (sprite.flipX)
      std::swap(source.u0, source.u1);
    if (sprite.flipY)
      std::swap(source.v0, source.v1);
    const float rotation = -worldRotation2D(world, *entity);
    const bool sliced = sprite.sliceStart.x > 0.0F ||
                        sprite.sliceStart.y > 0.0F ||
                        sprite.sliceEnd.x > 0.0F || sprite.sliceEnd.y > 0.0F;
    if (sliced && std::abs(rotation) < 0.0001F && !sprite.flipX &&
        !sprite.flipY) {
      if (!canvas_.ninePatch(
              texture.handle,
              {.x = screenX - sprite.pivot.x * width,
               .y = screenY - sprite.pivot.y * height,
               .width = width,
               .height = height},
              source,
              NinePatch2D{
                  .left = sprite.sliceStart.x,
                  .top = sprite.sliceStart.y,
                  .right = sprite.sliceEnd.x,
                  .bottom = sprite.sliceEnd.y,
                  .center =
                      {.u0 = (sourceX + sprite.sliceStart.x) / texture.width,
                       .v0 = (sourceY + sprite.sliceStart.y) / texture.height,
                       .u1 = (sourceX + sourceWidth - sprite.sliceEnd.x) /
                             texture.width,
                       .v1 = (sourceY + sourceHeight - sprite.sliceEnd.y) /
                             texture.height},
              },
              color, BlendMode::Alpha, scissor))
        return false;
    } else if (!canvas_.imageTransformed(texture.handle, screenX, screenY,
                                         width, height, sprite.pivot.x,
                                         sprite.pivot.y, rotation, source,
                                         color, BlendMode::Alpha, scissor)) {
      return false;
    }
  }
  return true;
}

} // namespace demi::runtime::render
