#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DCircle(const ui::UiNode &node,
                        const Hud3DRenderContext &context) {
  if (node.type != "circle")
    return false;
  const float centerX =
      (node.resolved.x + node.resolved.width * 0.5F) * context.scaleX;
  const float centerY =
      (node.resolved.y + node.resolved.height * 0.5F) * context.scaleY;
  DrawCircleV({centerX, centerY}, node.radius * context.textScale,
              toRlColor(node.color));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
