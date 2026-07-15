#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DPanel(const ui::UiNode &node,
                       const Hud3DRenderContext &context) {
  if (node.type != "rect" && node.type != "panel" && node.type != "container" &&
      node.type != "scroll" && node.type != "list" && node.type != "modal")
    return false;

  const Color fillColor =
      node.backgroundColor.a > 0.0F ? node.backgroundColor : node.color;
  if (fillColor.a <= 0.0F)
    return true;
  const ::Rectangle rect{
      .x = node.resolved.x * context.scaleX,
      .y = node.resolved.y * context.scaleY,
      .width = node.resolved.width * context.scaleX,
      .height = node.resolved.height * context.scaleY,
  };
  const float radius = node.cornerRadius * context.textScale;
  if (radius <= 0.0F) {
    DrawRectangleRec(rect, toRlColor(fillColor));
    if (node.borderWidth > 0.0F)
      DrawRectangleLinesEx(rect, node.borderWidth * context.textScale,
                           toRlColor(node.borderColor));
    return true;
  }

  const float roundness = std::min(
      radius / std::max(std::min(rect.width, rect.height) * 0.5F, 1.0F), 1.0F);
  DrawRectangleRounded(rect, roundness, 8, toRlColor(fillColor));
  if (node.borderWidth > 0.0F)
    DrawRectangleRoundedLinesEx(rect, roundness, 8,
                                node.borderWidth * context.textScale,
                                toRlColor(node.borderColor));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
