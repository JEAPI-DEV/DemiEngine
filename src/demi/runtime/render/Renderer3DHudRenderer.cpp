#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/ProfilerHudRenderer.h"
#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/ui/UiModel.h"

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

  for (const ui::UiNode &node : world.ui.nodes) {
    if (!node.visible)
      continue;
    if (node.type == "rect" && node.backgroundColor.a > 0.0F) {
      ::Rectangle rect{
          .x = node.resolved.x * scaleX,
          .y = node.resolved.y * scaleY,
          .width = node.resolved.width * scaleX,
          .height = node.resolved.height * scaleY,
      };
      DrawRectangleRec(rect,
                       renderer3d_detail::toRlColor(node.backgroundColor));
    } else if (node.type == "image" && !node.texture.empty()) {
      std::string textureId = node.texture;
      if (!node.animation.empty()) {
        const auto animation = imageAnimations_.find(node.animation);
        if (animation == imageAnimations_.end() || animation->second <= 0) {
          continue;
        }
        textureId = node.animation + "#" +
                    std::to_string(node.animationFrame % animation->second);
      }
      const auto texture = textures_.find(textureId);
      if (texture == textures_.end()) {
        continue;
      }
      const float sourceWidth = node.sourceSize.x != 0.0F
                                    ? node.sourceSize.x
                                    : static_cast<float>(texture->second.width);
      const float sourceHeight =
          node.sourceSize.y != 0.0F
              ? node.sourceSize.y
              : static_cast<float>(texture->second.height);
      const ::Rectangle source{
          .x = node.sourcePosition.x,
          .y = node.sourcePosition.y,
          .width = sourceWidth,
          .height = sourceHeight,
      };
      const ::Rectangle dest{
          .x = node.resolved.x * scaleX,
          .y = node.resolved.y * scaleY,
          .width = node.resolved.width * scaleX,
          .height = node.resolved.height * scaleY,
      };
      DrawTexturePro(texture->second, source, dest, ::Vector2{0.0F, 0.0F}, 0.0F,
                     renderer3d_detail::toRlColor(node.color));
    } else if (node.type == "label" || node.type == "text") {
      const float authoredFontSize = node.fontSize > 0.0F
                                         ? node.fontSize
                                         : node.scale * HudFontBaseSize;
      const float fontSize =
          std::max(authoredFontSize * textScale, HudFontMinSize);
      const Color textColor = node.textColor.a > 0.0F ? node.textColor
                                                       : node.color;
      DrawTextEx(
          GetFontDefault(), node.text.c_str(),
          Vector2{node.resolved.x * scaleX, node.resolved.y * scaleY},
          fontSize, HudLetterSpacing, renderer3d_detail::toRlColor(textColor));
    } else if (node.type == "button" || node.type == "toggle" ||
               node.type == "text_input") {
      const Color fillColor =
          node.hovered
              ? (node.hoverColor.a > 0.0F ? node.hoverColor
                                          : node.backgroundColor)
              : (node.backgroundColor.a > 0.0F ? node.backgroundColor
                                               : node.color);
      ::Rectangle rect{
          .x = node.resolved.x * scaleX,
          .y = node.resolved.y * scaleY,
          .width = node.resolved.width * scaleX,
          .height = node.resolved.height * scaleY,
      };
      DrawRectangleRec(rect, renderer3d_detail::toRlColor(fillColor));
      const float authoredFontSize = node.fontSize > 0.0F
                                         ? node.fontSize
                                         : node.scale * HudFontBaseSize;
      const float fontSize =
          std::max(authoredFontSize * textScale, HudFontMinSize);
      const ::Vector2 measured = MeasureTextEx(GetFontDefault(),
                                                node.text.c_str(), fontSize,
                                                HudLetterSpacing);
      DrawTextEx(GetFontDefault(), node.text.c_str(),
                 Vector2{rect.x + rect.width * 0.5F - measured.x * 0.5F,
                         rect.y + rect.height * 0.5F - measured.y * 0.5F},
                 fontSize, HudLetterSpacing,
                 renderer3d_detail::toRlColor(node.textColor));
    }
  }
  if (world.debug.profilerHud && RuntimeProfiler::enabled()) {
    drawProfilerHud(RuntimeProfiler::frameSummary(0.05), width_, height_);
  }
}
} // namespace demi::runtime
