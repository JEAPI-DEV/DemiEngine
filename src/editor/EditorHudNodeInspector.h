#pragma once

#include <array>
#include <string>
#include <unordered_map>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorHudInspectorState {
  std::string nodeId;
  std::array<char, 512> text{};
  std::array<char, 512> texture{};
  std::array<char, 256> font{};
  std::array<char, 256> style{};
  std::array<char, 512> placeholder{};
  std::array<char, 256> action{};
  std::array<char, 512> accessibilityLabel{};
  std::array<char, 512> accessibilityDescription{};
  std::array<char, 16> backgroundHex{};
  std::array<char, 16> textHex{};
  std::array<char, 16> borderHex{};
  std::array<char, 16> tintHex{};
  std::string syncedText;
  std::string syncedTexture;
  std::string syncedFont;
  std::string syncedStyle;
  std::string syncedPlaceholder;
  std::string syncedAction;
  std::string syncedAccessibilityLabel;
  std::string syncedAccessibilityDescription;
  std::string syncedBackgroundHex;
  std::string syncedTextHex;
  std::string syncedBorderHex;
  std::string syncedTintHex;
  std::unordered_map<std::string, std::array<char, 512>> prefabStrings;
  std::unordered_map<std::string, std::string> syncedPrefabStrings;
};

void drawEditorHudNodeInspector(EditorWorkspace &workspace, ImVec2 position,
                                ImVec2 size, EditorHudInspectorState &state,
                                std::string &notice, bool *open = nullptr);

} // namespace demi::editor
