#pragma once

namespace demi::editor {

struct EditorProfilerSnapshot;

// Draws resource gauges for the attached Play session above timed scopes.
void drawEditorProfilerOverview(const EditorProfilerSnapshot &snapshot);

} // namespace demi::editor
