#pragma once

#include "editor/EditorUiHost.h"

#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

void drawEditorViewport(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, EditorViewportArea &viewportArea,
                        std::string &notice);

} // namespace demi::editor
