#include "editor/EditorShortcutSettings.h"
#include "editor/EditorChrome.h"
#include "editor/EditorShortcutInput.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace demi::editor {
void drawEditorShortcutSettings(EditorKeyBindings &bindings,
                                EditorShortcutSettingsState &state) {
  ImGui::SeparatorText("Keyboard shortcuts");
  ImGui::TextWrapped(
      "Shortcuts act on the focused authoring view. Text fields "
      "keep standard text editing. Game View has a separate "
      "cursor-release binding. Conflicting shortcuts are refused.");
  ImGui::InputTextWithHint("##shortcut-search", "Search actions",
                           &state.filter);
  if (ImGui::Button("Reset all shortcuts")) {
    bindings = EditorKeyBindings{};
    state.error.clear();
  }
  if (ImGui::BeginTable("shortcut-bindings", 3,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * 13);
    ImGui::TableHeadersRow();
    for (const auto &definition : editorCommandDefinitions()) {
      if (!state.filter.empty() &&
          definition.label.find(state.filter) == std::string_view::npos)
        continue;
      ImGui::PushID(definition.id.data());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(definition.label.data());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", definition.description.data());
      ImGui::TableNextColumn();
      const auto label = bindings.label(definition.command);
      ImGui::TextWrapped("%s", label.empty() ? "Unbound" : label.c_str());
      ImGui::TableNextColumn();
      if (editorIconButton("change", EditorIcon::Settings, "Change shortcut")) {
        state.recording = definition.command;
        state.append = false;
        state.error.clear();
        ImGui::OpenPopup("Record shortcut");
      }
      ImGui::SameLine();
      if (editorIconButton("add-shortcut", EditorIcon::Add,
                           "Add alternative shortcut")) {
        state.recording = definition.command;
        state.append = true;
        state.error.clear();
        ImGui::OpenPopup("Record shortcut");
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(definition.required);
      if (editorIconButton("clear", EditorIcon::Delete, "Clear binding"))
        (void)bindings.assign(definition.command, {}, state.error);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (editorIconButton("reset", EditorIcon::Refresh, "Reset this action")) {
        const EditorKeyBindings defaults;
        (void)bindings.assign(definition.command,
                              defaults.bindings(definition.command),
                              state.error);
      }
      if (ImGui::BeginPopupModal("Record shortcut", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Press a shortcut for %s", definition.label.data());
        ImGui::TextUnformatted(state.append
                                   ? "Add an alternative. Esc cancels."
                                   : "Replace shortcuts. Esc cancels.");
        const auto apply = [&](EditorKeyChord chord) {
          auto keys = state.append ? bindings.bindings(definition.command)
                                   : std::vector<EditorKeyChord>{};
          keys.push_back(std::move(chord));
          if (!bindings.assign(definition.command, std::move(keys),
                               state.error))
            return;
          state.recording.reset();
          ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Bind Escape"))
          apply({"Escape", 0});
        ImGui::SameLine();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::Button("Cancel")) {
          state.recording.reset();
          ImGui::CloseCurrentPopup();
        } else if (auto chord = captureEditorKeyChord()) {
          apply(std::move(*chord));
        }
        if (!state.error.empty())
          ImGui::TextColored({1, .4F, .4F, 1}, "%s", state.error.c_str());
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (!state.error.empty())
    ImGui::TextWrapped("%s", state.error.c_str());
}
} // namespace demi::editor
