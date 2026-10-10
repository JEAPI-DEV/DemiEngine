#pragma once

#include <imgui.h>

namespace demi::editor {

struct EditorColorControlOptions {
  ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar;
};

// Edits float RGBA. Callers choose normalized or HDR flags and own encoding.
bool drawEditorColorControl(const char *label, float rgba[4],
                            const EditorColorControlOptions &options = {});

} // namespace demi::editor
