#include "editor/EditorActivity.h"
#include "editor/EditorWorkspaceLayout.h"

#include <cassert>
#include <cmath>

namespace {

bool near(const float left, const float right) {
  return std::abs(left - right) < 0.01F;
}

void verify(const float width, const float height) {
  const auto layout = demi::editor::editorWorkspaceLayout(width, height);
  assert(layout.menuHeight > 0 && layout.toolbarHeight > 0 &&
         layout.statusHeight > 0 && layout.dockspaceHeight > 0);
  assert(
      near(layout.contentTop + layout.dockspaceHeight, layout.contentBottom));
}

} // namespace

int main() {
  demi::editor::EditorActivity activity;
  assert(activity.needsFrame(10, false));
  assert(activity.needsFrame(10.5, false));
  assert(!activity.needsFrame(11, false));
  activity.notify(12);
  assert(activity.needsFrame(12, false));
  assert(!activity.needsFrame(13, false));
  assert(activity.needsFrame(20, true));
  assert(activity.needsFrame(21, true));
  assert(!activity.needsFrame(22, false));
  verify(1680, 945);
  verify(960, 600);
  verify(640, 480);
  verify(320, 240);
  assert(demi::editor::editorDisplayScale(96.0F) == 1.0F);
  assert(demi::editor::editorDisplayScale(192.0F) == 2.0F);
  assert(demi::editor::editorDisplayScale(48.0F) == 1.0F);
  assert(demi::editor::editorDisplayScale(144.0F, 1.5F) == 2.25F);
  assert(demi::editor::editorDisplayScale(0.0F) == 1.0F);
}
