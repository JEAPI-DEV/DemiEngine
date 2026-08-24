#pragma once

#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

enum class EditorProfilerCategory {
  Frame,
  Rendering,
  Scripting,
  Physics,
  Animation,
  Resources,
  Network,
  Input,
  Other
};

struct EditorProfilerRow {
  runtime::RuntimeProfiler::Entry entry;
  EditorProfilerCategory category = EditorProfilerCategory::Other;
  double averageMilliseconds = 0.0;
};

struct EditorProfilerSnapshot {
  bool attached = false;
  bool paused = false;
  bool gpuTimingAvailable = false;
  std::size_t frameCount = 0;
  std::vector<EditorProfilerRow> rows;
};

[[nodiscard]] std::string_view
editorProfilerCategoryLabel(EditorProfilerCategory category);
[[nodiscard]] EditorProfilerCategory
editorProfilerCategory(std::string_view scope);
[[nodiscard]] EditorProfilerSnapshot buildEditorProfilerSnapshot(
    bool attached, bool paused,
    std::vector<runtime::RuntimeProfiler::Entry> entries,
    std::size_t frameCount);
[[nodiscard]] std::vector<EditorProfilerRow>
filterEditorProfilerRows(const EditorProfilerSnapshot &snapshot,
                         std::string_view query,
                         EditorProfilerCategory category, bool allCategories);

} // namespace demi::editor
