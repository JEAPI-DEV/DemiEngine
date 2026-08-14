#include "editor/EditorPanelStyle.h"

namespace demi::editor {
namespace {

constexpr ImGuiWindowFlags PanelFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoBringToFrontOnFocus;

} // namespace

void beginEditorPanel(const char *id, const ImVec2 position, const ImVec2 size,
                      const ImGuiWindowFlags additionalFlags) {
  ImGui::SetNextWindowPos(position);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin(id, nullptr, PanelFlags | additionalFlags);
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    draw->AddRectFilled(min, {min.x + 3.0F, min.y + size.y}, EditorAccent);
  }
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
