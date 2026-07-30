#pragma once

#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/FontAtlas2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <cstdint>

namespace demi::runtime::render {

class DebugCanvasRenderer {
public:
  explicit DebugCanvasRenderer(Canvas2D &canvas,
                               const FontAtlas2D *font = nullptr);

  [[nodiscard]] bool drawWorld(const World &world,
                               const Camera2DComponent &camera,
                               Vec2 cameraPosition,
                               std::uint16_t viewportWidth,
                               std::uint16_t viewportHeight);
  [[nodiscard]] bool drawNavigation(const navigation::NavigationGrid2D &grid,
                                    const Camera2DComponent &camera,
                                    Vec2 cameraPosition,
                                    std::uint16_t viewportWidth,
                                    std::uint16_t viewportHeight);

private:
  Canvas2D &canvas_;
  const FontAtlas2D *font_;
};

} // namespace demi::runtime::render
