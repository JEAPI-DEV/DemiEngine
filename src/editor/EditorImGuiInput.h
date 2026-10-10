#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {
struct InputState;
}

namespace demi::editor {

// SDL relative capture can leave absolute coordinates fixed or clamped. Keep
// a virtual position so ImGui can derive unbounded drag deltas during capture.
class EditorPointerMotion {
public:
  runtime::Vec2 update(const runtime::InputState &input, bool relative);

private:
  runtime::Vec2 position_;
  bool initialized_ = false;
};

// Adapts one platform input snapshot to Dear ImGui's queued input API. Call
// before ImGui::NewFrame so wheel, text, and key events are visible together.
void submitEditorImGuiInput(const runtime::InputState &input);

} // namespace demi::editor
