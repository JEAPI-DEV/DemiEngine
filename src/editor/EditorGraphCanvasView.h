#pragma once

struct ImVec2;

namespace demi::editor {

// View-only scale. Authored node coordinates never use display pixels.
class EditorGraphCanvasView {
public:
  float zoom() const { return zoom_; }
  ImVec2 toDisplay(ImVec2 point) const;
  ImVec2 toDocument(ImVec2 point) const;
  ImVec2 zoomAt(float zoom, ImVec2 anchor, ImVec2 panning);
  ImVec2 wheelAt(float wheel, ImVec2 anchor, ImVec2 panning);
  void reset() { zoom_ = 1.0F; }

private:
  float zoom_ = 1.0F;
};

} // namespace demi::editor
