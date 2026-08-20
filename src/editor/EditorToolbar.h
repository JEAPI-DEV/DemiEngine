#pragma once

#include <imgui.h>

#include <string>

namespace demi::editor {

class EditorPlaySession;
class EditorWorkspace;

void drawEditorToolbar(ImVec2 position, ImVec2 size, EditorWorkspace &workspace,
                       EditorPlaySession &playSession, bool &showGameView,
                       bool &stepRequested, std::string &notice);

} // namespace demi::editor
