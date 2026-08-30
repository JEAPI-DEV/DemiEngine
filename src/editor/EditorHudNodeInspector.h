#pragma once

#include <array>
#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorHudInspectorState {
  std::string nodeId;
  std::array<char, 512> text{};
  std::array<char, 512> texture{};
};

void drawEditorHudNodeInspector(EditorWorkspace &workspace, ImVec2 position,
                                ImVec2 size, EditorHudInspectorState &state,
                                std::string &notice);

} // namespace demi::editor
