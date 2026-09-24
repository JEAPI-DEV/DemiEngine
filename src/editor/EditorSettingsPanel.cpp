#include "editor/EditorSettingsPanel.h"
#include "editor/EditorPreferencesStore.h"
#include <vector>
#include <algorithm>

#include <imgui.h>

namespace demi::editor {

void drawEditorSettingsPanel(bool &open, float &uiScale, EditorPreferences &preferences) {
  if (!open)
    return;
  ImGui::SetNextWindowSize({560.0F, 470.0F}, ImGuiCond_FirstUseEver);
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
    ImGui::SeparatorText("Code editor");
    const auto input=[](const char *label,std::string &value) {
      std::vector<char> buffer(value.size()+256,0);std::copy(value.begin(),value.end(),buffer.begin());
      if (ImGui::InputText(label,buffer.data(),buffer.size())) value=buffer.data();
    };
    input("Executable",preferences.codeEditor);
    ImGui::TextWrapped("Arguments are passed separately. Use {project} and {file}; no shell quoting is needed.");
    for (std::size_t i=0;i<preferences.codeEditorArguments.size();++i) {
      ImGui::PushID(int(i));input("Argument",preferences.codeEditorArguments[i]);ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) { preferences.codeEditorArguments.erase(preferences.codeEditorArguments.begin()+i); ImGui::PopID();break; }
      ImGui::PopID();
    }
    if (ImGui::Button("Add argument")) preferences.codeEditorArguments.emplace_back();
    ImGui::SameLine();
    if (ImGui::Button("VS Code defaults")) { preferences.codeEditor="code";preferences.codeEditorArguments={"--reuse-window","{project}","--goto","{file}"}; }
  }
  ImGui::End();
}

} // namespace demi::editor
