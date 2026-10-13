#pragma once

#include <imgui.h>

#include "editor/EditorCommands.h"
#include <functional>
#include <string>

namespace demi::editor {

class EditorPlaySession;
class EditorRunPanel;
class EditorWorkspace;
class EditorKeyBindings;

void drawEditorToolbar(ImVec2 position, ImVec2 size, EditorWorkspace &workspace,
                       EditorWorkspace &playWorkspace,
                       EditorPlaySession &playSession, EditorRunPanel &runPanel, bool &showGameView,
                       bool &stepRequested, std::string &notice,
                       const EditorKeyBindings &bindings,
                       EditorCommandContext context,
                       const std::function<void(EditorCommand)> &execute);

} // namespace demi::editor
