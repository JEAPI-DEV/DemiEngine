#include "editor/EditorTerrainInspector.h"
#include "demi/assets/TerrainAsset.h"

#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string_view>
#include <utility>

namespace demi::editor {
namespace {

std::string brushField(const char *label) {
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(-1.0F);
  return std::string("##") + label;
}

bool chooseBiome(const nlohmann::json &biomes, std::string &selected) {
  if (!ImGui::BeginCombo(brushField("Paint biome").c_str(),
                         selected.empty() ? "Choose biome" : selected.c_str()))
    return false;
  bool changed = false;
  for (const auto &[id, value] : biomes.items()) {
    (void)value;
    if (ImGui::Selectable(id.c_str(), selected == id)) {
      selected = id;
      changed = true;
    }
  }
  ImGui::EndCombo();
  return changed;
}

void chooseBrushLayer(EditorTerrainAuthoring &authoring) {
  const auto layers = authoring.draft().value(
      "layers", defaultEditorTerrainRecipe().at("layers"));
  const auto kind = authoring.brushLayerKind();
  std::string preview;
  for (const auto &layer : layers)
    if (layer.value("id", std::string{}) == authoring.brush.layer)
      preview = layer.value("name", authoring.brush.layer);
  if (!ImGui::BeginCombo(brushField("Target layer").c_str(),
                         preview.empty() ? "Choose layer" : preview.c_str()))
    return;
  for (const auto &layer : layers) {
    if (layer.value("kind", std::string{}) != kind)
      continue;
    const auto id = layer.at("id").get<std::string>();
    ImGui::PushID(id.c_str());
    ImGui::BeginDisabled(!layer.value("enabled", true));
    if (ImGui::Selectable(layer.value("name", id).c_str(),
                          id == authoring.brush.layer))
      authoring.brush.layer = id;
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

void chooseTerrainAsset(EditorWorkspace &workspace, std::string_view entityId,
                        const char *label) {
  if (!ImGui::BeginCombo(label, "Choose Terrain asset"))
    return;
  for (const auto &record : workspace.assetIndex().assets()) {
    if (record.manifest.type != "Terrain")
      continue;
    if (ImGui::Selectable(record.manifest.id.c_str()))
      workspace.requestTerrainAssetAssign(std::string(entityId),
                                          record.manifest.id);
  }
  ImGui::EndCombo();
}

void drawSceneAssetActions(EditorWorkspace &workspace,
                           const runtime::Entity &entity,
                           const runtime::Terrain3DComponent &terrain,
                           std::string &notice) {
  if (!terrain.asset.empty()) {
    ImGui::TextDisabled("SHARED TERRAIN ASSET");
    ImGui::TextWrapped("%s", terrain.asset.c_str());
    ImGui::TextWrapped("Graph and brush changes edit this asset for every "
                       "scene placement.");
    chooseTerrainAsset(workspace, entity.id, "Replace terrain asset");
    return;
  }
  if (terrain.recipe.is_null()) {
    ImGui::TextWrapped("Choose a Terrain asset for this entity.");
    chooseTerrainAsset(workspace, entity.id, "Terrain asset");
    if (ImGui::Button("Create Terrain asset..."))
      workspace.requestTerrainAssetCreate();
    if (ImGui::SmallButton("Use an inline procedural recipe")) {
      std::string error;
      notice = workspace.editValue({.entityId = entity.id,
                                    .component = "Terrain3D",
                                    .field = "recipe"},
                                   defaultEditorTerrainRecipe(), false, error)
                   ? "Procedural terrain recipe added"
                   : error;
    }
    return;
  }
  ImGui::TextDisabled("INLINE PROCEDURAL TERRAIN");
  const bool authored =
      workspace.sceneDocument().component(entity.id, "Terrain3D") != nullptr;
  ImGui::BeginDisabled(!authored);
  if (ImGui::Button("Extract to Terrain asset..."))
    workspace.requestTerrainAssetCreate(entity.id);
  ImGui::EndDisabled();
  if (!authored)
    ImGui::TextWrapped("Open the source prefab to extract its recipe.");
  chooseTerrainAsset(workspace, entity.id, "Replace with Terrain asset");
}

void drawBrush(EditorTerrainAuthoring &authoring) {
  const bool draftChanged = authoring.hasDraftChanges();
  const bool canPaint = !authoring.busy() && !authoring.stroking() &&
                        !draftChanged && authoring.surface();
  const auto biomes = authoring.draft().value(
      "biomes", defaultEditorTerrainRecipe().at("biomes"));

  ImGui::SeparatorText("Viewport brush");
  if (draftChanged)
    ImGui::TextWrapped("Generate the graph draft before painting on this "
                       "surface.");
  ImGui::BeginDisabled(!canPaint);
  constexpr std::array tools{"Select",  "Raise",          "Lower",
                             "Flatten", "Smooth",         "Paint biome",
                             "Protect", "Paint exclusion"};
  int mode = static_cast<int>(authoring.brush.mode);
  if (ImGui::Combo(brushField("Tool").c_str(), &mode, tools.data(),
                   static_cast<int>(tools.size())))
    authoring.brush.mode = static_cast<EditorTerrainBrush>(mode);
  if (authoring.brush.mode != EditorTerrainBrush::Select)
    chooseBrushLayer(authoring);
  ImGui::DragFloat(brushField("Radius").c_str(), &authoring.brush.radius, 0.1F,
                   0.01F, 1e7F, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  if (authoring.brush.mode != EditorTerrainBrush::Protect) {
    ImGui::SliderFloat(brushField("Strength").c_str(),
                       &authoring.brush.strength, 0.0F, 1.0F, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat(brushField("Falloff").c_str(), &authoring.brush.falloff,
                       0.0F, 4.0F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
  }
  if (authoring.brush.mode == EditorTerrainBrush::Biome)
    chooseBiome(biomes, authoring.brush.biome);
  if (authoring.brush.mode == EditorTerrainBrush::Exclusion) {
    constexpr std::array operations{"Exclude", "Erase"};
    int operation = authoring.brush.exclusionValue == 0.0F ? 1 : 0;
    if (ImGui::Combo(brushField("Mask operation").c_str(), &operation,
                     operations.data(), static_cast<int>(operations.size())))
      authoring.brush.exclusionValue = operation == 0 ? 1.0F : 0.0F;
    ImGui::TextWrapped("Exclude paints mask weight; Erase restores a "
                       "subsection. Strength and falloff blend each stamp.");
  }
  ImGui::EndDisabled();

  ImGui::Checkbox("Preview exclusions", &authoring.previewExclusions);
  static constexpr std::array maskNames{"None", "Biome", "Elevation", "Slope"};
  int mask = static_cast<int>(authoring.maskPreview);
  if (ImGui::Combo(brushField("Rule mask overlay").c_str(), &mask,
                   maskNames.data(), static_cast<int>(maskNames.size())))
    authoring.maskPreview = static_cast<EditorTerrainMaskPreview>(mask);
  if (authoring.maskPreview != EditorTerrainMaskPreview::None)
    ImGui::TextWrapped("The overlay samples generated terrain. Graph draft "
                       "changes appear after Generate.");
  ImGui::TextWrapped("Drag to preview the brush. Release records one undoable "
                     "stroke. Esc cancels; Alt+drag navigates.");
  if (authoring.brush.mode == EditorTerrainBrush::Flatten)
    ImGui::TextWrapped("Flatten captures height where the stroke begins.");
  if (authoring.brush.mode == EditorTerrainBrush::Protect)
    ImGui::TextWrapped(
        "Protect preserves the current surface through later "
        "generation changes. Clear snapshots in Graph settings.");
}

} // namespace

void drawEditorTerrainInspector(EditorWorkspace &workspace,
                                std::string &notice) {
  if (workspace.activeDocument() == EditorWorkspaceDocument::Scene) {
    const auto *entity = workspace.selectedEntity();
    const auto *terrain =
        entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
    if (entity && terrain)
      drawSceneAssetActions(workspace, *entity, *terrain, notice);
    if (terrain && !terrain->asset.empty()) {
      const auto *asset = findAsset(workspace.assetIndex().registry(), terrain->asset);
      if (asset && assets::isPreparedTerrainAsset(*asset)) {
        ImGui::TextWrapped("Prepared terrain is read-only. Open the authored "
                           "source project to edit its graph and brushes.");
        return;
      }
    }
    if (terrain && terrain->asset.empty() && terrain->recipe.is_null())
      return;
  }

  workspace.syncTerrainAuthoring();
  auto &authoring = workspace.terrainAuthoring();
  if (auto draftNotice = authoring.takeNotice(); !draftNotice.empty())
    notice = std::move(draftNotice);
  if (authoring.entityId().empty()) {
    ImGui::TextWrapped("Select a Terrain3D entity in the 3D scene.");
    return;
  }

  if (ImGui::Button("Open Terrain Graph"))
    workspace.requestTerrainGraphOpen();
  if (authoring.entityId() != workspace.selectedEntityId()) {
    ImGui::TextWrapped(
        "The open graph is editing terrain '%s'. Open this terrain's graph "
        "to change the editing target.",
        authoring.entityId().c_str());
    return;
  }
  if (workspace.terrainEditingAsset()) {
    ImGui::SameLine();
    ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
    if (ImGui::Button("Apply changes to asset")) {
      std::string error;
      if (!workspace.applyTerrainAssetChanges(error))
        notice = std::move(error);
    }
    ImGui::EndDisabled();
  }
  if (authoring.busy()) {
    ImGui::ProgressBar(authoring.progress(), {-1.0F, 0.0F},
                       authoring.stroking() ? "Updating brush stroke"
                                            : "Updating terrain");
    if (ImGui::Button("Cancel terrain operation"))
      authoring.cancel();
    ImGui::TextWrapped("Save and Play wait for terrain and collision updates.");
  }
  drawBrush(authoring);
}

} // namespace demi::editor
