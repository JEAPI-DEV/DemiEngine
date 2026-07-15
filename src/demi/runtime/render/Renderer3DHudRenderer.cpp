#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/ProfilerHudRenderer.h"
#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/render/hud3d/Hud3DNodeRenderer.h"
#include "demi/runtime/ui/UiPresentation.h"

#include <algorithm>

namespace demi::runtime {
void Renderer3D::drawHud(const World &world) {
  ProfileScope scope("Renderer3D.draw_hud");
  EndMode3D();

  const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
  const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);
  const float scaleX = static_cast<float>(width_) / canvasWidth;
  const float scaleY = static_cast<float>(height_) / canvasHeight;
  const renderer3d_detail::Hud3DRenderContext context{
      .scaleX = scaleX,
      .scaleY = scaleY,
      .textScale = std::min(scaleX, scaleY),
      .textures = textures_,
      .imageAnimations = imageAnimations_,
  };

  for (const ui::UiPresentationNode &presented :
       ui::buildUiPresentation(world.ui)) {
    if (!presented.visible)
      continue;
    renderer3d_detail::drawHud3DNode(*presented.node, context);
  }
  if (world.debug.profilerHud && RuntimeProfiler::enabled()) {
    drawProfilerHud(RuntimeProfiler::frameSummary(0.05), width_, height_);
  }
}
} // namespace demi::runtime
