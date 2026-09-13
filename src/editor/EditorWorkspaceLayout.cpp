#include "editor/EditorWorkspaceLayout.h"

#include "editor/EditorDesignTokens.h"

#include <algorithm>

namespace demi::editor {

EditorWorkspaceLayout editorWorkspaceLayout(const float requestedWidth,
                                            const float requestedHeight) {
  (void)requestedWidth;
  const float height = std::max(requestedHeight, 240.0F);
  EditorWorkspaceLayout layout{
      .menuHeight = EditorDesignTokens::MenuHeight,
      .toolbarHeight = EditorDesignTokens::ToolbarHeight,
      .statusHeight = EditorDesignTokens::StatusHeight};
  layout.contentTop = layout.menuHeight + layout.toolbarHeight;
  layout.contentBottom = height - layout.statusHeight;
  layout.dockspaceHeight =
      std::max(layout.contentBottom - layout.contentTop, 1.0F);
  return layout;
}

float editorFontSize(const float logicalDpi) {
  return std::clamp(15.0F * logicalDpi / 96.0F, 14.0F, 22.0F);
}

} // namespace demi::editor
