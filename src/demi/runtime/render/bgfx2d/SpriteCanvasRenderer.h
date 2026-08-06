#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/bgfx2d/TextureAnimation2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace demi::runtime::render {

class SpriteCanvasRenderer {
public:
  SpriteCanvasRenderer(
      Canvas2D &canvas, const TextureLibrary2D &textures,
      const std::unordered_map<std::string, TextureAnimation2D> *animations =
          nullptr,
      const MaterialLibrary *materials = nullptr);

  [[nodiscard]] bool draw(const World &world,
                          const Camera2DComponent &camera,
                          Vec2 cameraPosition, std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight,
                          float animationTime = 0.0F);

private:
  Canvas2D &canvas_;
  const TextureLibrary2D &textures_;
  const std::unordered_map<std::string, TextureAnimation2D> *animations_;
  const MaterialLibrary *materials_;
};

} // namespace demi::runtime::render
