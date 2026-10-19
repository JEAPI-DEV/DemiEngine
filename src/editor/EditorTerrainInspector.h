#pragma once
#include <string>
namespace demi::editor {
class EditorWorkspace;
bool drawGeneratedTerrainDetailInspector(EditorWorkspace &workspace,
                                         std::string &notice);
void drawEditorTerrainInspector(EditorWorkspace &workspace,
                                std::string &notice);
} // namespace demi::editor
