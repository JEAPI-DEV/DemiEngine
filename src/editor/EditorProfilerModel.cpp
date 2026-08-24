#include "editor/EditorProfilerModel.h"

#include <algorithm>
#include <cctype>

namespace demi::editor {
namespace {

bool begins(const std::string_view value, const std::string_view prefix) {
  return value.starts_with(prefix);
}

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view query) {
  if (query.empty())
    return true;
  std::string haystack(value);
  std::string needle(query);
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

} // namespace

std::string_view
editorProfilerCategoryLabel(const EditorProfilerCategory category) {
  switch (category) {
  case EditorProfilerCategory::Frame:
    return "Frame";
  case EditorProfilerCategory::Rendering:
    return "Rendering";
  case EditorProfilerCategory::Scripting:
    return "Scripting";
  case EditorProfilerCategory::Physics:
    return "Physics";
  case EditorProfilerCategory::Animation:
    return "Animation";
  case EditorProfilerCategory::Resources:
    return "Resources";
  case EditorProfilerCategory::Network:
    return "Network";
  case EditorProfilerCategory::Input:
    return "Input";
  case EditorProfilerCategory::Other:
    return "Other";
  }
  return "Other";
}

EditorProfilerCategory editorProfilerCategory(const std::string_view scope) {
  if (begins(scope, "Frame."))
    return EditorProfilerCategory::Frame;
  if (begins(scope, "Render") || begins(scope, "Renderer"))
    return EditorProfilerCategory::Rendering;
  if (begins(scope, "Lua") || begins(scope, "Script"))
    return EditorProfilerCategory::Scripting;
  if (begins(scope, "Physics"))
    return EditorProfilerCategory::Physics;
  if (begins(scope, "Animation"))
    return EditorProfilerCategory::Animation;
  if (begins(scope, "Asset") || begins(scope, "World.") || begins(scope, "UI."))
    return EditorProfilerCategory::Resources;
  if (begins(scope, "Network"))
    return EditorProfilerCategory::Network;
  if (begins(scope, "Input"))
    return EditorProfilerCategory::Input;
  return EditorProfilerCategory::Other;
}

EditorProfilerSnapshot buildEditorProfilerSnapshot(
    const bool attached, const bool paused,
    std::vector<runtime::RuntimeProfiler::Entry> entries,
    const std::size_t frameCount) {
  EditorProfilerSnapshot snapshot{.attached = attached,
                                  .paused = paused,
                                  .gpuTimingAvailable = false,
                                  .frameCount = frameCount};
  snapshot.rows.reserve(entries.size());
  for (runtime::RuntimeProfiler::Entry &entry : entries) {
    const double average =
        entry.calls > 0 ? entry.totalMilliseconds / entry.calls : 0.0;
    const EditorProfilerCategory category = editorProfilerCategory(entry.name);
    snapshot.rows.push_back({.entry = std::move(entry),
                             .category = category,
                             .averageMilliseconds = average});
  }
  std::ranges::sort(snapshot.rows, [](const auto &left, const auto &right) {
    if (left.entry.hasGauge != right.entry.hasGauge)
      return !left.entry.hasGauge;
    return left.averageMilliseconds > right.averageMilliseconds;
  });
  return snapshot;
}

std::vector<EditorProfilerRow> filterEditorProfilerRows(
    const EditorProfilerSnapshot &snapshot, const std::string_view query,
    const EditorProfilerCategory category, const bool allCategories) {
  std::vector<EditorProfilerRow> rows;
  for (const EditorProfilerRow &row : snapshot.rows)
    if ((allCategories || row.category == category) &&
        containsCaseInsensitive(row.entry.name, query))
      rows.push_back(row);
  return rows;
}

} // namespace demi::editor
