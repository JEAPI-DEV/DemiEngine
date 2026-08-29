#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace demi::editor {

struct EditorGpuViewCounters {
  std::uint16_t viewId = 0;
  std::string name;
  std::int64_t begin = 0;
  std::int64_t end = 0;
};

struct EditorGpuPassTiming {
  std::uint16_t viewId = 0;
  std::string name;
  double milliseconds = 0.0;
};

struct EditorGpuTimingSample {
  bool available = false;
  double totalMilliseconds = 0.0;
  std::vector<EditorGpuPassTiming> passes;
};

[[nodiscard]] EditorGpuTimingSample
buildEditorGpuTimingSample(std::int64_t timerFrequency,
                           const std::vector<EditorGpuViewCounters> &views,
                           std::uint16_t firstGameView = 8,
                           std::uint16_t lastGameView = 11);

} // namespace demi::editor
