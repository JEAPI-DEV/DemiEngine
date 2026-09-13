#pragma once

namespace demi::editor {

struct EditorWorkspaceLayout {
  float menuHeight = 0.0F;
  float toolbarHeight = 0.0F;
  float statusHeight = 0.0F;
  float contentTop = 0.0F;
  float contentBottom = 0.0F;
  float dockspaceHeight = 0.0F;
};

[[nodiscard]] EditorWorkspaceLayout editorWorkspaceLayout(float width,
                                                          float height);
[[nodiscard]] float editorFontSize(float logicalDpi);

} // namespace demi::editor
