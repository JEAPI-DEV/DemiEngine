#include "editor/EditorGpuTiming.h"

#include <algorithm>
#include <limits>

namespace demi::editor {

EditorGpuTimingSample buildEditorGpuTimingSample(
    const runtime::render::GraphicsFrameTimings &timings,
    const std::uint16_t firstGameView, const std::uint16_t lastGameView) {
  EditorGpuTimingSample sample;
  if (!timings.gpuMilliseconds || firstGameView > lastGameView) {
    return sample;
  }
  // The graphics owner already rejects repeated GPU frames and views whose
  // GPU frame ID differs. Keep that single authority for editor traces too.
  double earliest = std::numeric_limits<double>::max();
  for (const auto &view : timings.gpuViews) {
    if (view.viewId >= firstGameView && view.viewId <= lastGameView) {
      earliest = std::min(earliest, view.startMilliseconds);
    }
  }
  for (const auto &view : timings.gpuViews) {
    if (view.viewId < firstGameView || view.viewId > lastGameView) {
      continue;
    }
    sample.passes.push_back(
        {.viewId = view.viewId,
         .name = view.name,
         .milliseconds = view.milliseconds,
         .startMilliseconds = view.startMilliseconds - earliest});
    sample.totalMilliseconds += view.milliseconds;
  }
  sample.gpuFrame = timings.gpuFrame;
  sample.available = !sample.passes.empty();
  return sample;
}

} // namespace demi::editor
