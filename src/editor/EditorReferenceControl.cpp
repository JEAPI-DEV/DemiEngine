#include "editor/EditorReferenceControl.h"

#include <imgui.h>

#include <algorithm>

namespace demi::editor {

bool drawEditorReferenceControl(
    const char *label, std::span<const EditorReferenceChoice> choices,
    std::string &selected, bool allowNone, const char *payloadType) {
  bool changed = false;
  const auto current = std::ranges::find(choices, selected,
                                         &EditorReferenceChoice::id);
  const char *preview = selected.empty() ? "None" : selected.c_str();
  if (current != choices.end())
    preview = current->label.c_str();

  if (ImGui::BeginCombo(label, preview)) {
    if (allowNone && ImGui::Selectable("None", selected.empty())) {
      changed = !selected.empty();
      selected.clear();
    }
    for (const auto &choice : choices) {
      const bool isSelected = choice.id == selected;
      if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
        changed = !isSelected;
        selected = choice.id;
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (payloadType && ImGui::BeginDragDropTarget()) {
    if (const auto *payload = ImGui::AcceptDragDropPayload(payloadType)) {
      const auto reference = editorReferenceFromPayload(
          {static_cast<const char *>(payload->Data),
           static_cast<std::size_t>(payload->DataSize)}, choices);
      if (reference && *reference != selected) {
        selected = *reference;
        changed = true;
      }
    }
    ImGui::EndDragDropTarget();
  }
  if (payloadType && ImGui::IsItemHovered())
    ImGui::SetTooltip("Choose an object or drag it here from the Hierarchy.");
  return changed;
}

} // namespace demi::editor
