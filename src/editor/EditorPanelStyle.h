#pragma once

#include "editor/EditorDialogLayout.h"

#include <imgui.h>

namespace demi::editor {

inline constexpr ImU32 EditorAccent = IM_COL32(91, 158, 245, 255);
inline constexpr ImU32 EditorAccentSoft = IM_COL32(52, 102, 170, 170);

[[nodiscard]] bool beginEditorPanel(const char *id, ImVec2 initialPosition,
                                    ImVec2 initialSize, bool *open = nullptr,
                                    ImGuiWindowFlags additionalFlags = 0);
void prepareEditorDialog(EditorDialogLayoutSpec spec);
void beginEditorShellPanel(const char *id, ImVec2 position, ImVec2 size,
                           ImGuiWindowFlags additionalFlags = 0);
bool editorAdvancedSettings(const char *help);

void editorSectionTitle(const char *title, const char *detail = nullptr);
void disabledEditorButton(const char *label, const char *reason,
                          ImVec2 size = {});

} // namespace demi::editor
