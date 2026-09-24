#pragma once

#include <array>
#include <string>
#include <filesystem>
#include <optional>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorInspectorPanelState {
  std::array<char, 128> componentSearch{};
  std::optional<std::filesystem::path> openRequest;
};

void drawInspectorPanel(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, EditorInspectorPanelState &state,
                        std::string &notice, bool *open = nullptr);

} // namespace demi::editor
