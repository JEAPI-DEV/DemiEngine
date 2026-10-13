#include "editor/EditorWorkspaceLayout.h"

#include "editor/EditorDesignTokens.h"

#include <algorithm>
#include <cmath>

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

float editorDisplayScale(const float logicalDpi, const float userZoom) {
  const float density = std::isfinite(logicalDpi) && logicalDpi > 0
                            ? std::clamp(logicalDpi / 96.0F, 1.0F, 4.0F)
                            : 1.0F;
  const float zoom =
      std::isfinite(userZoom) ? std::clamp(userZoom, 1.0F, 2.5F) : 1.0F;
  return density * zoom;
}

} // namespace demi::editor
