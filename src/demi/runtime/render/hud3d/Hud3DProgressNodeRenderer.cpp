#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DProgress(const ui::UiNode &node,
                          const Hud3DRenderContext &context) {
  if (node.type != "slider" && node.type != "progress")
    return false;
  ::Rectangle track{
      .x = node.resolved.x * context.scaleX,
      .y = node.resolved.y * context.scaleY,
      .width = node.resolved.width * context.scaleX,
      .height = node.resolved.height * context.scaleY,
  };
  DrawRectangleRec(track, toRlColor(node.backgroundColor));
  const float range = std::max(node.maximum - node.minimum, 0.0001F);
  const float fraction =
      std::clamp((node.value - node.minimum) / range, 0.0F, 1.0F);
  track.width *= fraction;
  DrawRectangleRec(track, toRlColor(node.color));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
