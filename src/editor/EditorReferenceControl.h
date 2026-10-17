#pragma once

#include "editor/EditorInspectorModel.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

// Presentation only: callers own validation, commands and Undo/Redo.
bool drawEditorReferenceControl(const char *label,
                                std::span<const EditorReferenceChoice> choices,
                                std::string &selected, bool allowNone,
                                const char *payloadType = nullptr);

// Build potentially large source/asset choices only while a popup needs them
// or a dropped reference must be validated. The selected preview stays cheap.
bool drawEditorLazyReferenceControl(
    const char *label,
    const std::function<std::vector<EditorReferenceChoice>()> &choices,
    std::string &selected, bool allowNone, const char *payloadType = nullptr,
    std::string_view previewLabel = {});

} // namespace demi::editor
