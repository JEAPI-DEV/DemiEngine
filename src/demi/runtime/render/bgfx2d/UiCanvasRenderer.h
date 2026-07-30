#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/FontAtlas2D.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx2d/TextureAnimation2D.h"
#include "demi/runtime/ui/UiModel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace demi::runtime::render {

using UiTextureLookup = std::function<TextureView2D(std::string_view)>;

class UiCanvasRenderer {
public:
  UiCanvasRenderer(Canvas2D &canvas, FontAtlas2D &font,
                   UiTextureLookup textureLookup = {},
                   const std::unordered_map<std::string, TextureAnimation2D>
                       *animations = nullptr,
                   float animationTime = 0.0F);

  [[nodiscard]] bool draw(const ui::UiDocument &document,
                          std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight);

private:
  [[nodiscard]] ScissorRect clipFor(const ui::UiDocument &document,
                                    const ui::UiNode &node, float scaleX,
                                    float scaleY) const;
  [[nodiscard]] bool drawNode(const ui::UiNode &node, float scaleX,
                              float scaleY, ScissorRect scissor);
  [[nodiscard]] TextureView2D imageTexture(const ui::UiNode &node) const;

  Canvas2D &canvas_;
  FontAtlas2D &font_;
  UiTextureLookup textureLookup_;
  const std::unordered_map<std::string, TextureAnimation2D> *animations_;
  float animationTime_;
};

} // namespace demi::runtime::render
