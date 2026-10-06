#pragma once

#include "editor/EditorModuleCatalog.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace demi::editor {

class EditorWorkspace;

// One full-card hit target, shared by docked and graph-local palettes.
// Returns activation for callers that also support click/keyboard insertion.
[[nodiscard]] bool drawEditorModuleCard(const EditorModule &module);

struct EditorModulesPanelState {
  std::array<char, 128> search{};
  std::filesystem::path catalogProject;
  std::uint64_t catalogSourceRevision = 0;
  std::vector<EditorModule> catalog;
};

// A HUD palette includes built-in controls and reusable UI prefabs. The caller
// selects its context explicitly and controls availability separately from the
// per-user open preference.
[[nodiscard]] constexpr bool
editorPaletteIncludesKind(const EditorModuleKind context,
                          const EditorModuleKind entry) {
  return context == EditorModuleKind::TerrainNode
             ? entry == EditorModuleKind::TerrainNode
             : entry == EditorModuleKind::HudElement ||
                   entry == EditorModuleKind::UiPrefab;
}

void drawEditorPalettePanel(const EditorWorkspace &workspace,
                            EditorModulesPanelState &state,
                            EditorModuleKind context, bool *open = nullptr);

} // namespace demi::editor
