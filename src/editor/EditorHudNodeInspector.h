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
  std::array<char, 16> backgroundHex{};
  std::array<char, 16> textHex{};
  bool backgroundHexInitialized = false;
  bool textHexInitialized = false;
  float uniformPad = 0.0F;
  bool uniformPadInitialized = false;
};

void drawEditorHudNodeInspector(EditorWorkspace &workspace, ImVec2 position,
                                ImVec2 size, EditorHudInspectorState &state,
                                std::string &notice);

} // namespace demi::editor
