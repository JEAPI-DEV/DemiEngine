#pragma once

#include <imgui.h>

namespace demi::editor {

enum class EditorIcon {
  Refresh,
  Save,
  Undo,
  Redo,
  Play,
  Pause,
  Stop,
  Pointer,
  Move,
  Rotate,
  Scale,
  Frame,
  Camera,
  Grid,
  Eye,
  Folder,
  File,
  Add,
  Settings
};

[[nodiscard]] bool editorIconButton(const char *id, EditorIcon icon,
                                    const char *tooltip, bool selected = false,
                                    bool enabled = true,
                                    ImVec2 size = {30.0F, 30.0F});
void editorToolbarSeparator(float height = 24.0F);
[[nodiscard]] bool editorStageTab(const char *label, bool selected,
                                  ImVec2 size = {88.0F, 27.0F});
void drawEditorGlyph(ImDrawList &draw, EditorIcon icon, ImVec2 center,
                     ImU32 color, float scale = 1.0F);

} // namespace demi::editor
