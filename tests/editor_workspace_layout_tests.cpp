#include "editor/EditorActivity.h"
#include "editor/EditorViewportTargetDemand.h"
#include "editor/EditorWorkspaceLayout.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

namespace {

void targetPreparationHandoffSurvivesIdleAndFailedAttempts() {
  demi::editor::EditorActivity activity;
  demi::editor::EditorViewportTargetDemand target;
  assert(activity.needsFrame(0, false));
  assert(!activity.needsFrame(1, false));
  assert(!target.needsPreparation());

  // A newly visible docked document publishes its first area after target
  // preparation. Focus may already be settled and must not own this work.
  target.observe(640, 480);
  assert(target.needsPreparation());
  assert(activity.needsFrame(2, target.needsPreparation()));
  target.recordAttempt(640, 480);
  target.observe(640, 480);
  assert(!target.needsPreparation());
  // A slow initialization can outlast the ordinary input settling window.
  // Its first actual texture frame still completes, then the host can sleep.
  assert(!activity.needsFrame(4, target.needsPreparation()));

  target.observe(800, 600);
  assert(target.needsPreparation());
  target.recordAttempt(640, 480);
  assert(target.needsPreparation());
  target.recordAttempt(800, 600);
  assert(!target.needsPreparation());

  // Failed attempts are acknowledged too. An unchanged failing allocation
  // does not create an automatic idle retry loop; new activity may retry it.
  target.observe(1280, 720);
  assert(activity.needsFrame(5, target.needsPreparation()));
  target.recordAttempt(1280, 720);
  target.observe(1280, 720);
  assert(!activity.needsFrame(7, target.needsPreparation()));

  // Closing a document schedules resource release, and reopening the same
  // dimensions must subsequently schedule fresh preparation.
  target.observe(0, 0);
  assert(target.needsPreparation());
  target.clear();
  assert(!target.needsPreparation());
  target.observe(1280, 720);
  assert(target.needsPreparation());
  target.observe(1280, 0);
  assert(!target.needsPreparation());
}

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
  targetPreparationHandoffSurvivesIdleAndFailedAttempts();
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
