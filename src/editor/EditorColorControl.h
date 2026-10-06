#pragma once

#include <imgui.h>

namespace demi::editor {

struct EditorColorControlOptions {
  ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar;
  bool showPrecision = false;
  const char *precisionHelp = nullptr;
  float precisionStep = 0.001F;
  float minimum = 0.0F;
  float maximum = 1.0F;
};

// Edits float RGBA. Callers choose normalized or HDR bounds and own encoding.
bool drawEditorColorControl(const char *label, float rgba[4],
                            const EditorColorControlOptions &options = {});

} // namespace demi::editor
