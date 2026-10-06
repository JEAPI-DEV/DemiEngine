#include "editor/EditorTerrainPresetsPanel.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"
#include <imgui.h>

namespace demi::editor {
bool EditorTerrainPresetsPanel::draw(EditorWorkspace &workspace,
                                     std::string &notice, bool *open) {
  if (!beginEditorPanel("Terrain Presets", {0, 0}, {360, 600}, open)) {
    ImGui::End();
    return false;
  }
  auto &authoring = workspace.terrainAuthoring();
  const auto binding =
      workspace.projectPath().string() + ":" + authoring.entityId();
  if (binding_ != binding) {
    binding_ = binding;
    selected_.clear();
  }
  bool changed = false;
  ImGui::TextWrapped("Start with a landscape, then follow its comment nodes. "
                     "Edit the graph and Generate to try changes.");
  ImGui::TextWrapped("Apply changes to asset saves the shared terrain source.");
  for (const auto &failure : workspace.terrainPresetErrors())
    ImGui::TextWrapped("%s", failure.c_str());
  const bool busy = authoring.busy() || authoring.stroking();
  ImGui::BeginDisabled(busy || authoring.entityId().empty());
  for (bool builtin : {true, false}) {
    ImGui::SeparatorText(builtin ? "Starter landscapes" : "Project presets");
    bool any = false;
    for (const auto &preset : authoring.presets()) {
      if (preset.id.starts_with("builtin://") != builtin)
        continue;
      any = true;
      ImGui::PushID(preset.id.c_str());
      if (ImGui::Selectable(preset.name.c_str(), selected_ == preset.id))
        selected_ = preset.id;
      ImGui::TextWrapped("%s", preset.description.c_str());
      ImGui::Spacing();
      ImGui::PopID();
    }
    if (!any)
      ImGui::TextWrapped(
          "Project landscape presets will appear here when imported.");
  }
  const auto *chosen = authoring.preset(selected_);
  ImGui::BeginDisabled(!chosen);
  if (ImGui::Button("Apply and generate"))
    ImGui::OpenPopup("Apply landscape preset?");
  ImGui::EndDisabled();
  if (ImGui::IsPopupOpen("Apply landscape preset?")) {
    prepareEditorDialog({.preferredEm = {38, 13}, .minimumEm = {28, 11}});
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5F, 0.5F});
  }
  if (ImGui::BeginPopupModal("Apply landscape preset?", nullptr,
                             ImGuiWindowFlags_NoDocking)) {
    ImGui::TextWrapped("Replace the current graph and generation settings? "
                       "Unapplied graph edits will be replaced. Sculpt strokes "
                       "and painted regions are retained when compatible.");
    if (chosen)
      ImGui::Text("%s: %.0f x %.0f world units, %d x %d cells",
                  chosen->name.c_str(), chosen->size.x, chosen->size.y,
                  chosen->cellsX, chosen->cellsZ);
    if (ImGui::Button("Apply") && chosen) {
      std::string error;
      const auto before = authoring.draft();
      if (!workspace.applyTerrainPreset(chosen->id, error)) {
        notice =
            authoring.needsResizeDecision()
                ? "Preset is staged. Press Generate in Terrain Graph to choose "
                  "how to handle the changed grid."
                : error;
      } else {
        notice = "Generating " + chosen->name +
                 ". Read the graph comments to get started.";
      }
      changed = before != authoring.draft();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::Separator();
  if (const auto id = authoring.draft().value("preset_id", std::string{});
      !id.empty()) {
    const auto *applied = authoring.preset(id);
    ImGui::TextWrapped("Applied: %s",
                       applied ? applied->name.c_str() : id.c_str());
    if (ImGui::Button("Clear preset label")) {
      std::string error;
      changed = workspace.clearTerrainPreset(error);
      if (!changed)
        notice = error;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Remove preset provenance while retaining this "
                        "terrain's settings and edits.");
  }
  if (ImGui::Button("Undo terrain")) {
    std::string error;
    changed = workspace.undo(error);
    if (!changed)
      notice = error;
  }
  ImGui::SameLine();
  if (ImGui::Button("Redo terrain")) {
    std::string error;
    changed = workspace.redo(error);
    if (!changed)
      notice = error;
  }
  ImGui::TextWrapped(
      "Undo terrain restores an applied landscape. Comment edits "
      "use Undo graph until you Generate.");
  ImGui::EndDisabled();
  ImGui::End();
  return changed;
}
} // namespace demi::editor
