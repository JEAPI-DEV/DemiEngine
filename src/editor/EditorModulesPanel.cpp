#include "editor/EditorModulesPanel.h"
#include "editor/EditorChrome.h"

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
  ImGui::PushID(module.id.c_str());
  const float padding = 12.0F;
  const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
  const float textWidth = std::max(width - padding * 2.0F, 1.0F);
  const float titleWidth = std::max(textWidth - 28.0F, 1.0F);
  const ImVec2 title =
      ImGui::CalcTextSize(module.title.c_str(), nullptr, false, titleWidth);
  const ImVec2 description = ImGui::CalcTextSize(module.description.c_str(),
                                                 nullptr, false, textWidth);
  const float height =
      padding * 2.0F + std::max(title.y, 20.0F) + 8.0F + description.y;
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
  const bool activated = ImGui::Button("##module-card", {width, height});
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  const ImVec2 topLeft = ImGui::GetItemRectMin();
  const ImVec2 bottomRight = ImGui::GetItemRectMax();
  auto &draw = *ImGui::GetWindowDrawList();
  const ImU32 accent = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  draw.AddRect(topLeft, bottomRight,
               ImGui::IsItemHovered() ? accent
                                      : ImGui::GetColorU32(ImGuiCol_Border),
               6.0F);
  draw.AddLine({topLeft.x + 2.0F, topLeft.y + 8.0F},
               {topLeft.x + 2.0F, bottomRight.y - 8.0F}, accent, 3.0F);
  const EditorIcon icon =
      module.kind == EditorModuleKind::TerrainNode ? EditorIcon::Terrain
      : module.kind == EditorModuleKind::UiPrefab  ? EditorIcon::Prefab
                                                   : EditorIcon::Hud;
  drawEditorGlyph(draw, icon,
                  {topLeft.x + padding + 9.0F, topLeft.y + padding + 10.0F},
                  ImGui::GetColorU32(ImGuiCol_Text));
  draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
               {topLeft.x + padding + 28.0F, topLeft.y + padding},
               ImGui::GetColorU32(ImGuiCol_Text), module.title.c_str(), nullptr,
               titleWidth);
  draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
               {topLeft.x + padding,
                topLeft.y + padding + std::max(title.y, 20.0F) + 8.0F},
               ImGui::GetColorU32(ImGuiCol_TextDisabled),
               module.description.c_str(), nullptr, textWidth);
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(EditorModulePayload, module.id.c_str(),
                              module.id.size() + 1);
    ImGui::TextUnformatted(module.title.c_str());
    ImGui::EndDragDropSource();
  }
  ImGui::Spacing();
  ImGui::PopID();
  return activated;
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
