#include "editor/EditorWorkspaceLayout.h"

#include <cassert>
#include <cmath>

namespace {

bool near(const float left, const float right) {
  return std::abs(left - right) < 0.01F;
}

void verify(const float width, const float height) {
  const auto layout = demi::editor::editorWorkspaceLayout(width, height);
  assert(layout.leftWidth > 0 && layout.centerWidth > 0 &&
         layout.rightWidth > 0);
  assert(near(layout.leftWidth + layout.centerWidth + layout.rightWidth,
              std::max(width, 320.0F)));
  assert(layout.upperHeight > 0 && layout.bottomHeight > 0);
  assert(near(layout.contentTop + layout.upperHeight + layout.bottomHeight,
              layout.contentBottom));
  assert(layout.consoleWidth > 0 && layout.assetsWidth > 0);
  assert(near(layout.consoleWidth + layout.assetsWidth + layout.rightWidth,
              std::max(width, 320.0F)));
}

} // namespace

int main() {
  verify(1680, 945);
  verify(960, 600);
  verify(640, 480);
  verify(320, 240);
  assert(demi::editor::editorFontSize(96.0F) == 15.0F);
  assert(demi::editor::editorFontSize(192.0F) == 22.0F);
  assert(demi::editor::editorFontSize(48.0F) == 14.0F);
}
