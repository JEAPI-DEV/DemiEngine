#pragma once

#include <imgui.h>

namespace demi::editor {

inline constexpr ImU32 EditorAccent = IM_COL32(112, 78, 183, 255);
inline constexpr ImU32 EditorAccentSoft = IM_COL32(88, 64, 140, 170);

void beginEditorPanel(const char *id, ImVec2 position, ImVec2 size,
                      ImGuiWindowFlags additionalFlags = 0);
void editorSectionTitle(const char *title, const char *detail = nullptr);
void disabledEditorButton(const char *label, const char *reason,
                          ImVec2 size = {});

} // namespace demi::editor
