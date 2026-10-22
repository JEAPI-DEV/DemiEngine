#pragma once

#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

namespace demi::editor {

inline double editorGameCpuMilliseconds(
    const runtime::RuntimeProfiler::FrameCapture &frame) {
  double milliseconds = 0.0;
  for (const auto &entry : frame.entries) {
    // These are disjoint roots. Adding nested systems would double-count work.
    if (entry.name == "Frame.update" || entry.name == "Render.submit") {
      milliseconds += entry.totalMilliseconds;
    }
  }
  return milliseconds;
}

inline std::vector<runtime::RuntimeProfiler::ScopeEvent> editorGameCpuScopes(
    const runtime::RuntimeProfiler::FrameCapture &frame) {
  using Event = runtime::RuntimeProfiler::ScopeEvent;
  const auto excluded = [](std::string_view name) {
    return name.starts_with("Editor.") || name.starts_with("Backend.");
  };
  std::vector<const Event *> excludedParents;
  for (const auto &event : frame.scopes) {
    if (excluded(event.name)) {
      excludedParents.push_back(&event);
    }
  }
  std::vector<Event> result;
  double firstStart = std::numeric_limits<double>::max();
  std::size_t firstDepth = std::numeric_limits<std::size_t>::max();
  for (const auto &event : frame.scopes) {
    if (excluded(event.name)) {
      continue;
    }
    // Renderer scopes inside an authored editor viewport are not game work,
    // even though the shared renderer correctly uses the same scope names.
    const bool editorChild = std::ranges::any_of(
        excludedParents, [&](const Event *parent) {
          return event.depth > parent->depth &&
                 event.startMilliseconds >= parent->startMilliseconds &&
                 event.startMilliseconds + event.durationMilliseconds <=
                     parent->startMilliseconds + parent->durationMilliseconds;
        });
    if (editorChild) {
      continue;
    }
    firstStart = std::min(firstStart, event.startMilliseconds);
    firstDepth = std::min(firstDepth, event.depth);
    result.push_back(event);
  }
  for (auto &event : result) {
    event.startMilliseconds -= firstStart;
    event.depth -= firstDepth;
  }
  return result;
}

} // namespace demi::editor
