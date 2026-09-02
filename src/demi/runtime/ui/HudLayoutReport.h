#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/ui/UiModel.h"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace demi::runtime::ui {

// Resolved layout facts for one node of a HUD document.
struct HudNodeReport {
  std::string id;
  std::string type;
  std::string action;
  bool visible = true;
  bool focusable = false;
  Rect resolved;
};

// Canvas size and resolved node rects for a HUD document. Computed with the
// same parser and layout engine the runtime uses, so inspector output,
// editor views, and automation tooling agree with on-screen layout.
struct HudLayoutReport {
  Vec2 canvasSize{960.0F, 540.0F};
  std::vector<HudNodeReport> nodes;
  Diagnostics diagnostics;
};

struct HudLayoutRequest {
  // Safe-area insets in canvas units, matching the runtime's applied insets.
  Insets safeArea;
  // Resolve nodes inside hidden containers as if every container were
  // visible, so automation can target screens that gameplay reveals later.
  bool revealHidden = false;
};

[[nodiscard]] HudLayoutReport
inspectHudLayout(const nlohmann::json &document,
                 const HudLayoutRequest &request = {});

} // namespace demi::runtime::ui
