#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DText(const ui::UiNode &node,
                      const Hud3DRenderContext &context) {
  if (node.type != "label" && node.type != "text")
    return false;
  constexpr float FontBaseSize = 8.0F;
  constexpr float FontMinSize = 4.0F;
  constexpr float LetterSpacing = 5.0F;
  const float authoredSize =
      node.fontSize > 0.0F ? node.fontSize : node.scale * FontBaseSize;
  const float fontSize =
      std::max(authoredSize * context.textScale, FontMinSize);
  const Color color = node.textColor.a > 0.0F ? node.textColor : node.color;
  DrawTextEx(
      GetFontDefault(), node.text.c_str(),
      {node.resolved.x * context.scaleX, node.resolved.y * context.scaleY},
      fontSize, LetterSpacing, toRlColor(color));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
