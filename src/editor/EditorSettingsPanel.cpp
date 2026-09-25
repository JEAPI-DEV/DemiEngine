#include "editor/EditorSettingsPanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorPreferencesStore.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace demi::editor {
namespace {

constexpr EditorDialogLayoutSpec SettingsLayout{
    .preferredEm = {58.0F, 44.0F},
    .minimumEm = {38.0F, 28.0F},
};

bool inputText(const char *id, std::string &value) {
  std::vector<char> buffer(value.size() + 256, 0);
  std::copy(value.begin(), value.end(), buffer.begin());
  if (!ImGui::InputText(id, buffer.data(), buffer.size()))
    return false;
  value = buffer.data();
  return true;
}

} // namespace

void drawEditorSettingsPanel(bool &open, float &uiScale,
                             EditorPreferences &preferences) {
  if (!open)
    return;

  prepareEditorDialog(SettingsLayout);
  if (ImGui::Begin("Editor Settings", &open, ImGuiWindowFlags_NoDocking)) {
    ImGui::BeginChild("editor-settings-content", {}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::SeparatorText("Appearance");
    int percent = static_cast<int>(uiScale * 100.0F + 0.5F);
    if (ImGui::BeginTable("appearance-grid", 2,
                          ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                              ImGui::GetFontSize() * 11.0F);
      ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("UI scale");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      if (ImGui::SliderInt("##ui-scale", &percent, 100, 250, "%d%%",
                           ImGuiSliderFlags_AlwaysClamp))
        uiScale = static_cast<float>(percent) / 100.0F;
      ImGui::EndTable();
    }
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
    ImGui::TextWrapped("Arguments are passed separately. Use {project} and "
                       "{file}; no shell quoting is needed.");
    if (ImGui::BeginTable("code-editor-grid", 2,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                              ImGui::GetFontSize() * 11.0F);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Executable");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      inputText("##code-editor", preferences.codeEditor);

      for (std::size_t index = 0;
           index < preferences.codeEditorArguments.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Argument %zu", index + 1);
        ImGui::TableNextColumn();
        const float removeWidth = ImGui::CalcTextSize("Remove").x +
                                  ImGui::GetStyle().FramePadding.x * 2.0F;
        ImGui::SetNextItemWidth(
            -(removeWidth + ImGui::GetStyle().ItemSpacing.x));
        inputText("##argument", preferences.codeEditorArguments[index]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
          preferences.codeEditorArguments.erase(
              preferences.codeEditorArguments.begin() +
              static_cast<std::ptrdiff_t>(index));
          ImGui::PopID();
          break;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::Button("Add argument"))
      preferences.codeEditorArguments.emplace_back();
    ImGui::SameLine();
    if (ImGui::Button("VS Code defaults")) {
      preferences.codeEditor = "code";
      preferences.codeEditorArguments = {"--reuse-window", "{project}",
                                         "--goto", "{file}"};
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

} // namespace demi::editor
