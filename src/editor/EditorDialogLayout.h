#pragma once

#include <algorithm>

namespace demi::editor {

struct EditorDialogSize {
  float width = 0.0F;
  float height = 0.0F;
};

struct EditorDialogLayoutSpec {
  EditorDialogSize preferredEm;
  EditorDialogSize minimumEm;
  float viewportPaddingEm = 1.0F;
};

struct EditorDialogLayout {
  EditorDialogSize initial;
  EditorDialogSize minimum;
  EditorDialogSize maximum;
};

// Keeps dialog policy independent of ImGui so viewport and DPI edge cases can
// be covered without constructing an editor frame.
[[nodiscard]] constexpr EditorDialogLayout
editorDialogLayout(const EditorDialogSize workArea, const float fontSize,
                   const EditorDialogLayoutSpec spec) {
  const float em = std::max(fontSize, 1.0F);
  const float padding = std::max(spec.viewportPaddingEm, 0.0F) * em;
  const EditorDialogSize maximum{
      .width = std::max(workArea.width - padding * 2.0F, 1.0F),
      .height = std::max(workArea.height - padding * 2.0F, 1.0F),
  };
  const EditorDialogSize minimum{
      .width = std::clamp(spec.minimumEm.width * em, 1.0F, maximum.width),
      .height = std::clamp(spec.minimumEm.height * em, 1.0F, maximum.height),
  };
  return {
      .initial =
          {
              .width = std::clamp(spec.preferredEm.width * em, minimum.width,
                                  maximum.width),
              .height = std::clamp(spec.preferredEm.height * em, minimum.height,
                                   maximum.height),
          },
      .minimum = minimum,
      .maximum = maximum,
  };
}

} // namespace demi::editor
