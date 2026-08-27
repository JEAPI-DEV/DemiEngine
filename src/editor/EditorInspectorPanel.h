#pragma once

#include <array>
#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorInspectorPanelState {
  std::array<char, 128> componentSearch{};
};

void drawInspectorPanel(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, EditorInspectorPanelState &state,
                        std::string &notice);

} // namespace demi::editor
