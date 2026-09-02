#include "demi/runtime/ui/HudLayoutReport.h"

#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

#include <algorithm>

namespace demi::runtime::ui {

HudLayoutReport inspectHudLayout(const nlohmann::json &document,
                                 const HudLayoutRequest &request) {
  HudLayoutReport report;
  UiDocument hud = parseUiDocument(document);
  hud.safeArea = request.safeArea;
  if (request.revealHidden)
    for (UiNode &node : hud.nodes)
      node.visible = true;
  report.canvasSize = hud.canvasSize;
  UiLayoutEngine{}.layout(hud, hud.canvasSize);
  report.nodes.reserve(hud.nodes.size());
  for (const UiNode &node : hud.nodes) {
    report.nodes.push_back({.id = node.id,
                            .type = node.type,
                            .action = node.action,
                            .visible = node.visible,
                            .focusable = node.focusable,
                            .resolved = node.resolved});
  }
  std::ranges::sort(report.nodes, {}, &HudNodeReport::id);
  return report;
}

} // namespace demi::runtime::ui
