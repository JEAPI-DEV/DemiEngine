#include "editor/EditorReferenceControl.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

namespace demi::editor {

namespace {
using ChoicesProvider = std::function<std::span<const EditorReferenceChoice>()>;

bool drawReferenceControl(const char *label, const ChoicesProvider &choices,
                          std::string &selected, bool allowNone,
                          const char *payloadType,
                          std::string_view previewLabel) {
  bool changed = false;
  std::string preview = selected.empty() ? "None" : selected;
  if (!selected.empty() && !previewLabel.empty()) {
    preview = previewLabel;
  }
  if (ImGui::BeginCombo(label, preview.c_str())) {
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
    for (const auto &choice : choices()) {
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
          choices());
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

} // namespace

bool drawEditorReferenceControl(const char *label,
                                std::span<const EditorReferenceChoice> choices,
                                std::string &selected, bool allowNone,
                                const char *payloadType) {
  const auto current =
      std::ranges::find(choices, selected, &EditorReferenceChoice::id);
  const std::string_view preview = current == choices.end()
                                       ? std::string_view{}
                                       : std::string_view(current->label);
  return drawReferenceControl(
      label, [choices] { return choices; }, selected, allowNone, payloadType,
      preview);
}

bool drawEditorLazyReferenceControl(
    const char *label,
    const std::function<std::vector<EditorReferenceChoice>()> &choices,
    std::string &selected, bool allowNone, const char *payloadType,
    std::string_view previewLabel) {
  std::optional<std::vector<EditorReferenceChoice>> prepared;
  const auto provide = [&]() -> std::span<const EditorReferenceChoice> {
    if (!prepared) {
      prepared = choices();
    }
    return *prepared;
  };
  return drawReferenceControl(label, provide, selected, allowNone, payloadType,
                              previewLabel);
}

} // namespace demi::editor
