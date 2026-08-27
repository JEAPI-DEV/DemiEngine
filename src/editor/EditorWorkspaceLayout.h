#pragma once

namespace demi::editor {

struct EditorWorkspaceLayout {
  float menuHeight = 0.0F;
  float toolbarHeight = 0.0F;
  float stageTabsHeight = 0.0F;
  float statusHeight = 0.0F;
  float leftWidth = 0.0F;
  float centerWidth = 0.0F;
  float rightWidth = 0.0F;
  float upperHeight = 0.0F;
  float bottomHeight = 0.0F;
  float consoleWidth = 0.0F;
  float assetsWidth = 0.0F;
  float buildWidth = 0.0F;
  float contentTop = 0.0F;
  float contentBottom = 0.0F;
};

[[nodiscard]] EditorWorkspaceLayout editorWorkspaceLayout(float width,
                                                          float height);
[[nodiscard]] float editorFontSize(float logicalDpi);

} // namespace demi::editor
