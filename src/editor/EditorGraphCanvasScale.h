#pragma once
#include <imnodes.h>

namespace demi::editor {
// Scale actual widgets and pins together; hit testing remains library-owned.
class EditorGraphCanvasScale {
public:
  explicit EditorGraphCanvasScale(float zoom);
  ~EditorGraphCanvasScale();
  EditorGraphCanvasScale(const EditorGraphCanvasScale &) = delete;
  EditorGraphCanvasScale &operator=(const EditorGraphCanvasScale &) = delete;

private:
  ImNodesStyle nodeStyle_;
};
} // namespace demi::editor
