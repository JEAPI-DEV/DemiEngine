#pragma once

#include "demi/runtime/render/hud3d/Hud3DNodeRenderer.h"

namespace demi::runtime::renderer3d_detail {

[[nodiscard]] bool tryDrawHud3DPanel(const ui::UiNode &node,
                                     const Hud3DRenderContext &context);
[[nodiscard]] bool tryDrawHud3DImage(const ui::UiNode &node,
                                     const Hud3DRenderContext &context);
[[nodiscard]] bool tryDrawHud3DCircle(const ui::UiNode &node,
                                      const Hud3DRenderContext &context);
[[nodiscard]] bool tryDrawHud3DProgress(const ui::UiNode &node,
                                        const Hud3DRenderContext &context);
[[nodiscard]] bool tryDrawHud3DText(const ui::UiNode &node,
                                    const Hud3DRenderContext &context);
[[nodiscard]] bool tryDrawHud3DButton(const ui::UiNode &node,
                                      const Hud3DRenderContext &context);

} // namespace demi::runtime::renderer3d_detail
