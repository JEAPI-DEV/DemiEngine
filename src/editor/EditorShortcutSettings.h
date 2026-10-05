#pragma once
#include "editor/EditorKeyBindings.h"
#include <optional>
#include <string>

namespace demi::editor {
struct EditorShortcutSettingsState {
  std::optional<EditorCommand> recording;
  bool append = false;
  std::string filter;
  std::string error;
};
void drawEditorShortcutSettings(EditorKeyBindings &bindings,
                                EditorShortcutSettingsState &state);
} // namespace demi::editor
