#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/ProfilerHudRenderer.h"
#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>
#include <string>

namespace demi::runtime {
void Renderer3D::drawHud(const World &world) {
  ProfileScope scope("Renderer3D.draw_hud");
  EndMode3D();

  const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
  const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);
  const float scaleX = static_cast<float>(width_) / canvasWidth;
  const float scaleY = static_cast<float>(height_) / canvasHeight;
  const float textScale = std::min(scaleX, scaleY);
  constexpr float HudFontBaseSize = 8.0F;
  constexpr float HudFontMinSize = 4.0F;
  constexpr float HudLetterSpacing = 5.0F;

  for (const HudRectElement &element : world.hudRects) {
    if (!element.visible) {
      continue;
    }
    ::Rectangle rect{
        .x = element.position.x * scaleX,
        .y = element.position.y * scaleY,
        .width = element.size.x * scaleX,
        .height = element.size.y * scaleY,
    };
    DrawRectangleRec(rect, renderer3d_detail::toRlColor(element.color));
  }

  for (const HudImageElement &element : world.hudImages) {
    if (!element.visible || element.texture.empty()) {
      continue;
    }
    std::string textureId = element.texture;
    if (!element.animation.empty()) {
      const auto animation = imageAnimations_.find(element.animation);
      if (animation == imageAnimations_.end() || animation->second <= 0) {
        continue;
      }
      textureId = element.animation + "#" +
                  std::to_string(element.animationFrame % animation->second);
    }
    const auto texture = textures_.find(textureId);
    if (texture == textures_.end()) {
      continue;
    }
    const float sourceWidth = element.sourceSize.x != 0.0F
                                  ? element.sourceSize.x
                                  : static_cast<float>(texture->second.width);
    const float sourceHeight = element.sourceSize.y != 0.0F
                                   ? element.sourceSize.y
                                   : static_cast<float>(texture->second.height);
    const ::Rectangle source{
        .x = element.sourcePosition.x,
        .y = element.sourcePosition.y,
        .width = sourceWidth,
        .height = sourceHeight,
    };
    const ::Rectangle dest{
        .x = element.position.x * scaleX,
        .y = element.position.y * scaleY,
        .width = element.size.x * scaleX,
        .height = element.size.y * scaleY,
    };
    DrawTexturePro(texture->second, source, dest, ::Vector2{0.0F, 0.0F}, 0.0F,
                   renderer3d_detail::toRlColor(element.color));
  }

  for (const HudTextElement &element : world.hudText) {
    if (!element.visible) {
      continue;
    }
    const float authoredFontSize = element.fontSize > 0.0F
                                       ? element.fontSize
                                       : element.scale * HudFontBaseSize;
    const float fontSize =
        std::max(authoredFontSize * textScale, HudFontMinSize);
    DrawTextEx(
        GetFontDefault(), element.text.c_str(),
        Vector2{element.position.x * scaleX, element.position.y * scaleY},
        fontSize, HudLetterSpacing,
        renderer3d_detail::toRlColor(element.color));
  }

  for (const HudButtonElement &element : world.hudButtons) {
    if (!element.visible) {
      continue;
    }
    const ::Color color = renderer3d_detail::toRlColor(
        element.hovered ? element.hoverColor : element.color);
    ::Rectangle rect{
        .x = element.position.x * scaleX,
        .y = element.position.y * scaleY,
        .width = element.size.x * scaleX,
        .height = element.size.y * scaleY,
    };
    DrawRectangleRec(rect, color);
    const float authoredFontSize = element.fontSize > 0.0F
                                       ? element.fontSize
                                       : element.scale * HudFontBaseSize;
    const float fontSize =
        std::max(authoredFontSize * textScale, HudFontMinSize);
    const ::Vector2 measured = MeasureTextEx(
        GetFontDefault(), element.label.c_str(), fontSize, HudLetterSpacing);
    DrawTextEx(GetFontDefault(), element.label.c_str(),
               Vector2{rect.x + rect.width * 0.5F - measured.x * 0.5F,
                       rect.y + rect.height * 0.5F - measured.y * 0.5F},
               fontSize, HudLetterSpacing,
               renderer3d_detail::toRlColor(element.textColor));
  }
  if (world.debug.profilerHud && RuntimeProfiler::enabled()) {
    drawProfilerHud(RuntimeProfiler::frameSummary(0.05), width_, height_);
  }
}
} // namespace demi::runtime
