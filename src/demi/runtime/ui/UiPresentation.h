#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <vector>

namespace demi::runtime::ui {

struct UiPresentationNode {
  const UiNode *node = nullptr;
  int effectiveLayer = 0;
  bool visible = false;
};

// Layout-oriented nodes are transparent unless they declare a background.
// Visual panels retain `color` as the convenient fill fallback.
[[nodiscard]] Color uiPanelFillColor(const UiNode &node);

// Resolves hierarchy-dependent presentation state and returns nodes in stable
// back-to-front order. Invalid or cyclic parent chains are kept but hidden.
[[nodiscard]] std::vector<UiPresentationNode>
buildUiPresentation(const UiDocument &document);

} // namespace demi::runtime::ui
