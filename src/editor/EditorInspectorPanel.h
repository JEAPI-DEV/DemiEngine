#pragma once

#include <imgui.h>

#include <string>

namespace demi::editor {

class EditorWorkspace;

void drawInspectorPanel(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, std::string &notice);

} // namespace demi::editor
