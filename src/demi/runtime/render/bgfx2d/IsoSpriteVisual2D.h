#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"

#include <cstdint>

namespace demi::runtime::render {

enum class IsoSpriteShape2D { Rectangle, Circle, Triangle };

struct IsoSpriteVisual2D {
  Rect2D bounds;
  IsoSpriteShape2D shape = IsoSpriteShape2D::Rectangle;
  std::uint32_t color = 0xffffffffU;
};

[[nodiscard]] IsoSpriteVisual2D
isoSpriteVisual2D(const SpriteComponent *sprite, Vec2 anchor, float width,
                  float height);

} // namespace demi::runtime::render
