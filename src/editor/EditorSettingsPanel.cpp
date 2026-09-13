#include "editor/EditorSettingsPanel.h"

#include <imgui.h>

namespace demi::editor {

void drawEditorSettingsPanel(bool &open, float &uiScale) {
  if (!open)
    return;
  ImGui::SetNextWindowSize({420.0F, 205.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Editor Settings", &open, ImGuiWindowFlags_NoDocking)) {
    ImGui::SeparatorText("Appearance");
    int percent = static_cast<int>(uiScale * 100.0F + 0.5F);
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::SliderInt("UI scale", &percent, 100, 250, "%d%%",
                         ImGuiSliderFlags_AlwaysClamp))
      uiScale = static_cast<float>(percent) / 100.0F;
    if (ImGui::Button("100%"))
      uiScale = 1.0F;
    ImGui::SameLine();
    if (ImGui::Button("150%"))
      uiScale = 1.5F;
    ImGui::SameLine();
    if (ImGui::Button("200%"))
      uiScale = 2.0F;
    ImGui::Spacing();
    ImGui::TextWrapped("Scales text, icons, and controls across the editor. "
                       "Scene and Game views retain full pixel resolution.");
    ImGui::TextDisabled("Saved automatically for your user, not the project.");
  }
  ImGui::End();
}

} // namespace demi::editor
