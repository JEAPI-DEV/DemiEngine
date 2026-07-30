#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <cstdint>

namespace demi::runtime::render {

// Preserves the game-facing primitive presentation used by scenes whose
// debug-visible collider is intentionally their only visual.
class ColliderCanvasRenderer {
public:
  explicit ColliderCanvasRenderer(Canvas2D &canvas);

  [[nodiscard]] bool draw(const World &world, const Camera2DComponent &camera,
                          Vec2 cameraPosition, std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight);

private:
  Canvas2D &canvas_;
};

} // namespace demi::runtime::render
