#pragma once

#include "editor/EditorInspectorModel.h"

#include <span>
#include <string>

namespace demi::editor {

// Presentation only: callers own validation, commands and Undo/Redo.
bool drawEditorReferenceControl(const char *label,
                                std::span<const EditorReferenceChoice> choices,
                                std::string &selected, bool allowNone,
                                const char *payloadType = nullptr);

} // namespace demi::editor
