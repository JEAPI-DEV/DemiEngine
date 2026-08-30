#pragma once

namespace demi::runtime {
struct InputState;
}

namespace demi::editor {

// Adapts one platform input snapshot to Dear ImGui's queued input API. Call
// before ImGui::NewFrame so wheel, text, and key events are visible together.
void submitEditorImGuiInput(const runtime::InputState &input);

} // namespace demi::editor
