#include "demi/runtime/render/hud3d/Hud3DNodeRenderer.h"

#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include <array>

namespace demi::runtime::renderer3d_detail {

void drawHud3DNode(const ui::UiNode &node, const Hud3DRenderContext &context) {
  using NodeRenderer = bool (*)(const ui::UiNode &, const Hud3DRenderContext &);
  constexpr std::array<NodeRenderer, 6> renderers{
      &tryDrawHud3DPanel,    &tryDrawHud3DImage, &tryDrawHud3DCircle,
      &tryDrawHud3DProgress, &tryDrawHud3DText,  &tryDrawHud3DButton,
  };
  for (const NodeRenderer renderer : renderers)
    if (renderer(node, context))
      return;
}

} // namespace demi::runtime::renderer3d_detail
