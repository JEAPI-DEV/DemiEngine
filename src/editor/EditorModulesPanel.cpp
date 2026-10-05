#include "editor/EditorModulesPanel.h"
#include "editor/EditorChrome.h"
#include "editor/EditorPaletteCard.h"

#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorModuleCatalog.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {
namespace {

bool containsIgnoreCase(const std::string_view text,
                        const std::string_view query) {
  return std::search(text.begin(), text.end(), query.begin(), query.end(),
                     [](const char left, const char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                     }) != text.end();
}

bool matches(const EditorModule &module, const std::string_view query) {
  return query.empty() || containsIgnoreCase(module.title, query) ||
         containsIgnoreCase(module.category, query) ||
         containsIgnoreCase(module.description, query);
}

} // namespace

bool drawEditorModuleCard(const EditorModule &module) {
  const EditorIcon icon =
      module.kind == EditorModuleKind::TerrainNode ? EditorIcon::Terrain
      : module.kind == EditorModuleKind::UiPrefab  ? EditorIcon::Prefab
                                                   : EditorIcon::Hud;
  const EditorPaletteCard card{module.id, module.title, module.description,
                               icon};
  const auto bytes =
      std::as_bytes(std::span(module.id.c_str(), module.id.size() + 1));
  return drawEditorPaletteCard(card, EditorModulePayload, bytes);
}

void drawEditorPalettePanel(const EditorWorkspace &workspace,
                            EditorModulesPanelState &state,
                            const EditorModuleKind context, bool *open) {
  const bool terrain = context == EditorModuleKind::TerrainNode;
  const char *windowName = terrain ? "Terrain Nodes" : "UI Palette";
  if (!beginEditorPanel(windowName, {0.0F, 0.0F}, {360.0F, 600.0F}, open)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("Drag %s onto the %s.",
                      terrain ? "a node" : "a control or UI prefab",
                      terrain ? "terrain graph" : "HUD canvas");
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##palette-search",
                           terrain ? "Search terrain nodes..."
                                   : "Search UI controls and prefabs...",
                           state.search.data(), state.search.size());
  ImGui::Separator();

  if (state.catalogProject != workspace.projectPath() ||
      state.catalogSourceRevision != workspace.sourceIndexRevision()) {
    state.catalog = editorModules(workspace);
    state.catalogProject = workspace.projectPath();
    state.catalogSourceRevision = workspace.sourceIndexRevision();
  }
  const auto &catalog = state.catalog;
  std::vector<std::string_view> categories;
  for (const EditorModule &module : catalog) {
    if (!editorPaletteIncludesKind(context, module.kind) ||
        !matches(module, state.search.data()))
      continue;
    if (std::find(categories.begin(), categories.end(), module.category) ==
        categories.end())
      categories.push_back(module.category);
  }
  for (const std::string_view category : categories) {
    ImGui::SeparatorText(std::string(category).c_str());
    for (const EditorModule &module : catalog)
      if (module.category == category &&
          editorPaletteIncludesKind(context, module.kind) &&
          matches(module, state.search.data()))
        (void)drawEditorModuleCard(module);
  }
  if (categories.empty())
    ImGui::TextDisabled("No %s match your search.",
                        terrain ? "nodes" : "controls or UI prefabs");
  ImGui::End();
}

} // namespace demi::editor
