#pragma once

#include <string>

namespace demi::editor {

class EditorWorkspace;

// Draws the specialized adapter for a virtual painted-cell selection. Returns
// false when the current selection is a normal entity.
[[nodiscard]] bool drawIsoGridCellInspector(EditorWorkspace &workspace,
                                            std::string &notice);

} // namespace demi::editor
