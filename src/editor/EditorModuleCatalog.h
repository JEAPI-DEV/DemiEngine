#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

class EditorWorkspace;

enum class EditorModuleKind { HudElement, UiPrefab, TerrainNode };

struct EditorModule {
  std::string id;
  EditorModuleKind kind;
  std::string category;
  std::string title;
  std::string description;
  std::string icon;
  // HUD type, ui-prefab:// reference, or terrain registry node ID.
  std::string value;
};

// Terrain entries come from the graph's node registry. The catalog owns
// presentation metadata and stable IDs, never registry or ImGui pointers.
[[nodiscard]] std::vector<EditorModule>
editorModules(const EditorWorkspace &workspace);
[[nodiscard]] const EditorModule *
resolveModule(std::span<const EditorModule> modules, std::string_view id);

} // namespace demi::editor
