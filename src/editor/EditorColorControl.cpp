#include "editor/EditorColorControl.h"
#include <algorithm>

namespace demi::editor {
bool drawEditorColorControl(const char *label, float rgba[4],
                            const EditorColorControlOptions &options) {
  const bool changed = ImGui::ColorEdit4(label, rgba, options.flags);
  if (changed && !(options.flags & ImGuiColorEditFlags_HDR))
    for (int channel = 0; channel < 4; ++channel)
      rgba[channel] = std::clamp(rgba[channel], 0.0F, 1.0F);
  return changed;
}
} // namespace demi::editor
