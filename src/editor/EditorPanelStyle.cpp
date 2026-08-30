#include "editor/EditorPanelStyle.h"

namespace demi::editor {
namespace {

constexpr ImGuiWindowFlags PanelFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

} // namespace

void beginEditorPanel(const char *id, const ImVec2 position, const ImVec2 size,
                      const ImGuiWindowFlags additionalFlags) {
  ImGui::SetNextWindowPos(position);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin(id, nullptr, PanelFlags | additionalFlags);
}

void editorSectionTitle(const char *title, const char *detail) {
  ImGui::TextUnformatted(title);
  if (detail != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", detail);
  }
  ImGui::Separator();
}

void disabledEditorButton(const char *label, const char *reason,
                          const ImVec2 size) {
  ImGui::BeginDisabled();
  ImGui::Button(label, size);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", reason);
}

} // namespace demi::editor
