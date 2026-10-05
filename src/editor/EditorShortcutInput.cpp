#include "editor/EditorShortcutInput.h"
#include <imgui.h>

namespace demi::editor {
namespace {
unsigned modifiers() {
  const auto &input = ImGui::GetIO();
  return (input.KeyCtrl ? Control : 0) | (input.KeyShift ? Shift : 0) |
         (input.KeyAlt ? Alt : 0) | (input.KeySuper ? Super : 0);
}
ImGuiKey keyFor(std::string_view name) {
  if (name == "Left")
    return ImGuiKey_LeftArrow;
  if (name == "Right")
    return ImGuiKey_RightArrow;
  if (name == "Up")
    return ImGuiKey_UpArrow;
  if (name == "Down")
    return ImGuiKey_DownArrow;
  if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
    return static_cast<ImGuiKey>(ImGuiKey_A + name[0] - 'A');
  if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
    return static_cast<ImGuiKey>(ImGuiKey_0 + name[0] - '0');
  for (int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_NamedKey_END;
       ++value) {
    const auto key = static_cast<ImGuiKey>(value);
    if (name == ImGui::GetKeyName(key))
      return key;
  }
  return ImGuiKey_None;
}
} // namespace

bool editorShortcutPressed(const EditorKeyBindings &bindings,
                           EditorCommand command,
                           EditorCommandContext context) {
  if (!editorCommandAvailable(command, context))
    return false;
  for (const auto &chord : bindings.bindings(command)) {
    const auto key = keyFor(chord.key);
    if (key != ImGuiKey_None && chord.modifiers == modifiers() &&
        ImGui::IsKeyPressed(key, false))
      return true;
  }
  return false;
}

std::optional<EditorKeyChord> captureEditorKeyChord() {
  for (int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_NamedKey_END;
       ++value) {
    const auto key = static_cast<ImGuiKey>(value);
    if (!ImGui::IsKeyPressed(key, false))
      continue;
    std::string error;
    const std::string_view name = key == ImGuiKey_LeftArrow    ? "Left"
                                  : key == ImGuiKey_RightArrow ? "Right"
                                  : key == ImGuiKey_UpArrow    ? "Up"
                                  : key == ImGuiKey_DownArrow
                                      ? "Down"
                                      : ImGui::GetKeyName(key);
    auto chord = parseEditorKeyChord(name, error);
    if (chord) {
      chord->modifiers = modifiers();
      return chord;
    }
  }
  return {};
}
} // namespace demi::editor
