#pragma once

namespace demi::editor {
struct EditorPreferences;
struct EditorShortcutSettingsState;
void drawEditorSettingsPanel(bool &open, float &uiScale,
                             EditorPreferences &preferences,
                             EditorShortcutSettingsState &shortcuts);
} // namespace demi::editor
