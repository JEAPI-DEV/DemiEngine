#include "editor/EditorPanelStyle.h"

#include "editor/EditorDesignTokens.h"

#include <cfloat>

namespace demi::editor {
namespace {

constexpr ImGuiWindowFlags DockablePanelFlags = ImGuiWindowFlags_NoCollapse;
constexpr ImGuiWindowFlags ShellPanelFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoDocking;

} // namespace

bool beginEditorPanel(const char *id, const ImVec2 initialPosition,
                      const ImVec2 initialSize, bool *open,
                      const ImGuiWindowFlags additionalFlags) {
  ImGui::SetNextWindowPos(initialPosition, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints({EditorDesignTokens::MinimumPanelWidth,
                                       EditorDesignTokens::MinimumPanelHeight},
                                      {FLT_MAX, FLT_MAX});
  return ImGui::Begin(id, open, DockablePanelFlags | additionalFlags);
}

void beginEditorShellPanel(const char *id, const ImVec2 position,
                           const ImVec2 size,
                           const ImGuiWindowFlags additionalFlags) {
  ImGui::SetNextWindowPos(position, ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::Begin(id, nullptr, ShellPanelFlags | additionalFlags);
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
