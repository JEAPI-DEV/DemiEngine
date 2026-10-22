#pragma once
#include "editor/EditorRenderViews.h"
#include "demi/runtime/render/backend/GraphicsFrameTimings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demi::editor {

struct EditorGpuPassTiming {
  std::uint16_t viewId = 0;
  std::string name;
  double milliseconds = 0.0;
  double startMilliseconds = 0.0;
};

struct EditorGpuTimingSample {
  bool available = false;
  double totalMilliseconds = 0.0;
  std::uint32_t gpuFrame = 0;
  std::vector<EditorGpuPassTiming> passes;
};

[[nodiscard]] EditorGpuTimingSample
buildEditorGpuTimingSample(
    const runtime::render::GraphicsFrameTimings &timings,
    std::uint16_t firstGameView = EditorGameFirstView,
    std::uint16_t lastGameView = EditorGameLastView);

} // namespace demi::editor
