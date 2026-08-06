#include "demi/runtime/render/bgfx2d/IsoSpriteVisual2D.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

namespace demi::runtime::render {

IsoSpriteVisual2D isoSpriteVisual2D(const SpriteComponent *sprite,
                                    const Vec2 anchor, const float width,
                                    const float height) {
  const Vec2 pivot = sprite != nullptr ? sprite->pivot : Vec2{0.5F, 1.0F};
  IsoSpriteShape2D shape = IsoSpriteShape2D::Rectangle;
  if (sprite != nullptr && sprite->shape == "circle")
    shape = IsoSpriteShape2D::Circle;
  else if (sprite != nullptr && sprite->shape == "triangle")
    shape = IsoSpriteShape2D::Triangle;
  return {.bounds = {.x = anchor.x - pivot.x * width,
                     .y = anchor.y - pivot.y * height,
                     .width = width,
                     .height = height},
          .shape = shape,
          .color = sprite != nullptr ? packVertexColorRgba8(sprite->color)
                                    : 0xff52a6deU};
}

} // namespace demi::runtime::render
