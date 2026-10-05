#pragma once

#include "editor/EditorKeyBindings.h"

namespace demi::editor {
bool editorShortcutPressed(const EditorKeyBindings &bindings,
                           EditorCommand command, EditorCommandContext context);
std::optional<EditorKeyChord> captureEditorKeyChord();
} // namespace demi::editor
