#include "editor/EditorColorControl.h"

namespace demi::editor {

bool drawEditorColorControl(const char *label, float rgba[4],
                            const EditorColorControlOptions &options) {
  const bool hasRange = options.maximum > options.minimum;
  bool changed = ImGui::ColorEdit4(label, rgba, options.flags);
  if (!options.showPrecision)
    return changed;

  ImGui::PushID(label);
  if (ImGui::TreeNode("RGBA precision")) {
    if (options.precisionHelp != nullptr)
      ImGui::TextWrapped("%s", options.precisionHelp);
    ImGui::SetNextItemWidth(-1.0F);
    changed |=
        ImGui::DragFloat4("##normalized-rgba", rgba, options.precisionStep,
                          options.minimum, options.maximum, "%.6f",
                          ImGuiSliderFlags_NoRoundToFormat |
                              (hasRange ? ImGuiSliderFlags_AlwaysClamp : 0));
    ImGui::TreePop();
  }
  ImGui::PopID();
  return changed;
}

} // namespace demi::editor
