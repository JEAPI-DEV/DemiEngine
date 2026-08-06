#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <cstdint>
#include <unordered_map>

namespace demi::runtime::render {

class TilemapCanvasRenderer {
public:
  TilemapCanvasRenderer(
      Canvas2D &canvas, const TextureLibrary2D &textures,
      const std::unordered_map<std::string, TilemapAsset2D> &tilemaps);

  [[nodiscard]] bool draw(const World &world,
                          const Camera2DComponent &camera,
                          Vec2 cameraPosition, std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight,
                          float animationTime);

private:
  Canvas2D &canvas_;
  const TextureLibrary2D &textures_;
  const std::unordered_map<std::string, TilemapAsset2D> &tilemaps_;
};

} // namespace demi::runtime::render
