#include "editor/EditorGraphCanvasView.h"
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace demi::editor {

ImVec2 EditorGraphCanvasView::toDisplay(ImVec2 point) const {
  return {point.x * zoom_, point.y * zoom_};
}

ImVec2 EditorGraphCanvasView::toDocument(ImVec2 point) const {
  return {point.x / zoom_, point.y / zoom_};
}

ImVec2 EditorGraphCanvasView::zoomAt(float zoom, ImVec2 anchor,
                                     ImVec2 panning) {
  if (!std::isfinite(zoom))
    return panning;
  const auto point = toDocument({anchor.x - panning.x, anchor.y - panning.y});
  // Navigation bounds keep widgets usable; they impose no graph/content limit.
  zoom_ = std::clamp(zoom, 0.2F, 2.5F);
  const auto display = toDisplay(point);
  return {anchor.x - display.x, anchor.y - display.y};
}

ImVec2 EditorGraphCanvasView::wheelAt(float wheel, ImVec2 anchor,
                                      ImVec2 panning) {
  if (!std::isfinite(wheel))
    return panning;
  return zoomAt(zoom_ * std::exp(std::clamp(wheel, -20.0F, 20.0F) * 0.12F),
                anchor, panning);
}

} // namespace demi::editor
