#include "editor/EditorGpuTiming.h"

namespace demi::editor {

EditorGpuTimingSample
buildEditorGpuTimingSample(const std::int64_t timerFrequency,
                           const std::vector<EditorGpuViewCounters> &views,
                           const std::uint16_t firstGameView,
                           const std::uint16_t lastGameView) {
  EditorGpuTimingSample sample;
  if (timerFrequency <= 0 || firstGameView > lastGameView)
    return sample;
  for (const EditorGpuViewCounters &view : views) {
    if (view.viewId < firstGameView || view.viewId > lastGameView ||
        view.end <= view.begin)
      continue;
    const double milliseconds = static_cast<double>(view.end - view.begin) *
                                1000.0 / static_cast<double>(timerFrequency);
    sample.passes.push_back({.viewId = view.viewId,
                             .name = view.name.empty()
                                         ? "view_" + std::to_string(view.viewId)
                                         : view.name,
                             .milliseconds = milliseconds});
    sample.totalMilliseconds += milliseconds;
  }
  sample.available = !sample.passes.empty();
  return sample;
}

} // namespace demi::editor
