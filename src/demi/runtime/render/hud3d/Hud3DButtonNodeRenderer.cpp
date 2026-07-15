#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DButton(const ui::UiNode &node,
                        const Hud3DRenderContext &context) {
  if (node.type != "button" && node.type != "toggle" &&
      node.type != "text_input")
    return false;
  if (node.disabled)
    return true;

  constexpr float FontBaseSize = 8.0F;
  constexpr float FontMinSize = 4.0F;
  constexpr float LetterSpacing = 5.0F;
  const Color fillColor =
      node.hovered
          ? (node.hoverColor.a > 0.0F ? node.hoverColor : node.backgroundColor)
          : (node.backgroundColor.a > 0.0F ? node.backgroundColor : node.color);
  const ::Rectangle rect{
      .x = node.resolved.x * context.scaleX,
      .y = node.resolved.y * context.scaleY,
      .width = node.resolved.width * context.scaleX,
      .height = node.resolved.height * context.scaleY,
  };
  DrawRectangleRec(rect, toRlColor(fillColor));
  const float authoredSize =
      node.fontSize > 0.0F ? node.fontSize : node.scale * FontBaseSize;
  const float fontSize =
      std::max(authoredSize * context.textScale, FontMinSize);
  const ::Vector2 measured = MeasureTextEx(GetFontDefault(), node.text.c_str(),
                                           fontSize, LetterSpacing);
  DrawTextEx(GetFontDefault(), node.text.c_str(),
             {rect.x + (rect.width - measured.x) * 0.5F,
              rect.y + (rect.height - measured.y) * 0.5F},
             fontSize, LetterSpacing, toRlColor(node.textColor));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
