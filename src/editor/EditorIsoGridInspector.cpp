#include "editor/EditorIsoGridInspector.h"

#include "editor/EditorInspectorModel.h"
#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoGridCellDocument.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/Component.h"

#include <imgui.h>

#include <array>
#include <string>

namespace demi::editor {

bool drawIsoGridCellInspector(EditorWorkspace &workspace, std::string &notice) {
  const auto &selected = workspace.selectedIsoGridCell();
  if (!selected)
    return false;
  const nlohmann::json *component =
      workspace.sceneDocument().component(selected->gridEntityId, "IsoGrid");
  if (component == nullptr) {
    ImGui::TextDisabled("The selected painted cell no longer exists.");
    return true;
  }
  const std::string key = isoGridCellKey(selected->x, selected->y);
  const auto texture = authoredIsoGridCellTexture(*component, *selected);
  if (!texture) {
    ImGui::TextDisabled("The selected painted cell no longer exists.");
    return true;
  }

  ImGui::TextUnformatted("Painted Grid Cell");
  ImGui::TextDisabled("%s / %s", selected->gridEntityId.c_str(), key.c_str());
  ImGui::Separator();
  std::array<int, 2> coordinate{selected->x, selected->y};
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::InputInt2("Cell", coordinate.data())) {
    std::string error;
    notice =
        workspace.moveSelectedIsoGridCell(coordinate[0], coordinate[1], error)
            ? "Painted cell moved"
            : error;
  }

  std::string selectedTexture = *texture;
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::BeginCombo("Texture", selectedTexture.c_str())) {
    for (const EditorReferenceChoice &choice : editorReferenceChoices(
             runtime::ComponentReferenceKind::Asset,
             workspace.project().project.projectDirectory,
             workspace.sceneDocument().path(), workspace.sceneDocument().json(),
             workspace.sources())) {
      const bool isSelected = selectedTexture == choice.id;
      if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
        std::string error;
        notice = workspace.setSelectedIsoGridCellTexture(choice.id, error)
                     ? "Painted cell texture changed"
                     : error;
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Spacing();
  if (ImGui::Button("Clear painted cell")) {
    std::string error;
    notice = workspace.deleteSelectedIsoGridCell(error) ? "Painted cell cleared"
                                                        : error;
  }
  ImGui::TextDisabled(
      "Move with the tile-axis gizmo or enter a cell coordinate.");
  return true;
}

} // namespace demi::editor
