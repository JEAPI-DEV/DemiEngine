#include "editor/EditorReferenceControl.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace demi::editor {

bool drawEditorReferenceControl(const char *label,
                                std::span<const EditorReferenceChoice> choices,
                                std::string &selected, bool allowNone,
                                const char *payloadType) {
  bool changed = false;
  const auto current =
      std::ranges::find(choices, selected, &EditorReferenceChoice::id);
  const char *preview = selected.empty() ? "None" : selected.c_str();
  if (current != choices.end())
    preview = current->label.c_str();

  if (ImGui::BeginCombo(label, preview)) {
    static std::array<char, 128> filter{};
    if (ImGui::IsWindowAppearing()) {
      filter.fill('\0');
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##search", "Search...", filter.data(),
                             filter.size());
    const auto matches = [&](std::string value) {
      std::string query(filter.data());
      const auto lower = [](unsigned char c) { return char(std::tolower(c)); };
      std::ranges::transform(value, value.begin(), lower);
      std::ranges::transform(query, query.begin(), lower);
      return value.find(query) != std::string::npos;
    };
    if (allowNone && ImGui::Selectable("None", selected.empty())) {
      changed = !selected.empty();
      selected.clear();
    }
    for (const auto &choice : choices) {
      if (!matches(choice.label + " " + choice.id))
        continue;
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
           static_cast<std::size_t>(payload->DataSize)},
          choices);
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
