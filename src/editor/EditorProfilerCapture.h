#pragma once

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "editor/EditorProfilerScale.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace demi::editor {

// Owns inspection state only. Captured measurements belong to RuntimeProfiler.
class EditorProfilerCapture {
public:
  [[nodiscard]] const runtime::RuntimeProfiler::FrameCapture *draw();

private:
  runtime::RuntimeProfiler::CaptureHistory frozen_;
  std::uint64_t generation_ = 0;
  int selected_ = 0;
  bool inspecting_ = false;
  bool gpu_ = false;
  float zoom_ = 1.0F;
  EditorProfilerScale cpuScale_{16.67};
  EditorProfilerScale gpuScale_{16.67};
  EditorProfilerScale historyScale_{33.33};
  double lastDetailRefresh_ = -1.0;
  std::shared_ptr<const runtime::RuntimeProfiler::FrameCapture> liveDetail_;
  std::vector<float> historyTimes_;
  std::shared_ptr<const runtime::RuntimeProfiler::FrameCapture> projectedDetail_;
  std::vector<runtime::RuntimeProfiler::ScopeEvent> gameScopes_;
};

} // namespace demi::editor
