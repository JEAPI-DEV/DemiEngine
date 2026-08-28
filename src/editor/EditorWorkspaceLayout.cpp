#include "editor/EditorWorkspaceLayout.h"

#include <algorithm>

namespace demi::editor {

EditorWorkspaceLayout editorWorkspaceLayout(const float requestedWidth,
                                            const float requestedHeight) {
  const float width = std::max(requestedWidth, 320.0F);
  const float height = std::max(requestedHeight, 240.0F);
  EditorWorkspaceLayout layout{.menuHeight = 32.0F,
                               .toolbarHeight = 52.0F,
                               .stageTabsHeight = 31.0F,
                               .statusHeight = 27.0F};
  const bool narrow = width < 1000.0F;
  layout.leftWidth =
      std::clamp(width * 0.195F, narrow ? 150.0F : 240.0F, 340.0F);
  layout.rightWidth =
      std::clamp(width * 0.225F, narrow ? 190.0F : 285.0F, 405.0F);
  constexpr float MinimumCenter = 240.0F;
  if (layout.leftWidth + layout.rightWidth + MinimumCenter > width) {
    const float available = std::max(width - MinimumCenter, 1.0F);
    const float scale = available / (layout.leftWidth + layout.rightWidth);
    layout.leftWidth *= scale;
    layout.rightWidth *= scale;
  }
  layout.centerWidth = width - layout.leftWidth - layout.rightWidth;
  layout.contentTop = layout.menuHeight + layout.toolbarHeight;
  layout.contentBottom = height - layout.statusHeight;
  layout.bottomHeight =
      std::clamp(height * 0.30F, height < 700.0F ? 145.0F : 205.0F, 292.0F);
  const float verticalSpace =
      std::max(layout.contentBottom - layout.contentTop, 1.0F);
  layout.bottomHeight =
      std::min(layout.bottomHeight, std::max(verticalSpace - 80.0F, 60.0F));
  layout.upperHeight = std::max(verticalSpace - layout.bottomHeight, 1.0F);

  const float lowerWidth = width - layout.rightWidth;
  layout.consoleWidth =
      std::clamp(width * 0.26F, narrow ? 150.0F : 310.0F, 470.0F);
  constexpr float MinimumAssets = 120.0F;
  layout.consoleWidth =
      std::min(layout.consoleWidth, std::max(lowerWidth - MinimumAssets, 1.0F));
  layout.assetsWidth = lowerWidth - layout.consoleWidth;
  return layout;
}

float editorFontSize(const float logicalDpi) {
  return std::clamp(15.0F * logicalDpi / 96.0F, 14.0F, 22.0F);
}

} // namespace demi::editor
