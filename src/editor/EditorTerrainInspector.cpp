#include "editor/EditorTerrainInspector.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorWorkspace.h"
#include <algorithm>
#include <array>
#include <imgui.h>
#include <optional>
#include <vector>

namespace demi::editor {
namespace {
std::string terrainField(const char *label) {
  ImGui::TextWrapped("%s", label);
  ImGui::SetNextItemWidth(-1.0F);
  return std::string("##") + label;
}

void floatField(nlohmann::json &object, const char *key, const char *label,
                float fallback, float speed, float minimum, float maximum) {
  float value = object.value(key, fallback);
  if (ImGui::DragFloat(terrainField(label).c_str(), &value, speed, minimum,
                       maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp))
    object[key] = value;
}
bool biomeCombo(const char *label, const nlohmann::json &biomes,
                std::string &selected) {
  bool changed = false;
  if (ImGui::BeginCombo(terrainField(label).c_str(),
                        selected.empty() ? "Choose biome" : selected.c_str())) {
    for (const auto &[id, value] : biomes.items()) {
      (void)value;
      if (ImGui::Selectable(id.c_str(), id == selected)) {
        selected = id;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void drawClearTerrainDialog(const char *title, nlohmann::json &draft,
                            bool protectionOnly) {
  if (!ImGui::BeginPopupModal(title, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextWrapped(protectionOnly
                         ? "Remove every protection snapshot from the draft? "
                           "Sculpt edits and painted regions remain."
                         : "Remove all sculpt edits, protection snapshots and "
                           "painted regions from the draft?");
  ImGui::TextWrapped("Regenerate applies the removal as an undoable recipe "
                     "edit. The current preview stays visible until then.");
  if (ImGui::Button("Remove from draft")) {
    if (protectionOnly && draft.contains("edits")) {
      auto &edits = draft["edits"];
      for (auto edit = edits.begin(); edit != edits.end();) {
        if (edit->value("type", std::string{}) == "protect")
          edit = edits.erase(edit);
        else
          ++edit;
      }
    } else if (!protectionOnly) {
      draft["edits"] = nlohmann::json::array();
      draft["regions"] = nlohmann::json::array();
    }
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}
} // namespace

void drawEditorTerrainInspector(EditorWorkspace &workspace,
                                std::string &notice) {
  workspace.syncTerrainAuthoring();
  auto &authoring = workspace.terrainAuthoring();
  if (auto draftNotice = authoring.takeNotice(); !draftNotice.empty())
    notice = std::move(draftNotice);
  if (authoring.entityId().empty()) {
    ImGui::TextWrapped("Terrain tools require the 3D scene stage.");
    return;
  }
  auto &draft = authoring.draft();
  static const nlohmann::json emptyArray = nlohmann::json::array();
  const auto defaults = defaultEditorTerrainRecipe();
  ImGui::TextWrapped("Generation settings are a draft. Generate applies them "
                     "as one undoable recipe edit.");
  bool dirty = authoring.hasDraftChanges();
  if (dirty)
    ImGui::TextColored({1.0F, 0.75F, 0.35F, 1.0F}, "Unapplied draft settings");
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  const char *generateLabel = authoring.surface() ? "Regenerate" : "Generate";
  if (ImGui::Button(generateLabel)) {
    if (authoring.needsResizeDecision())
      ImGui::OpenPopup("Terrain grid changed");
    else {
      std::string error;
      if (!authoring.generate(std::nullopt, error))
        notice = std::move(error);
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!dirty);
  if (ImGui::Button("Discard draft"))
    authoring.discardDraft();
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (authoring.busy()) {
    ImGui::ProgressBar(authoring.progress(), {-1, 0}, "Generating terrain");
    if (ImGui::Button("Cancel generation"))
      authoring.cancel();
    ImGui::TextWrapped("The previous terrain stays visible until completion.");
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  auto size = draft.value("size", defaults.at("size"));
  float dimensions[]{size.at(0).get<float>(), size.at(1).get<float>()};
  if (ImGui::DragFloat2(terrainField("Size (X/Z)").c_str(), dimensions, 1.0F,
                        0.01F, 1e7F, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    draft["size"] = {dimensions[0], dimensions[1]};
  auto resolution = draft.value("resolution", defaults.at("resolution"));
  int cells[]{resolution.at(0).get<int>(), resolution.at(1).get<int>()};
  if (ImGui::InputInt2(terrainField("Resolution (cells)").c_str(), cells))
    draft["resolution"] = {cells[0], cells[1]};
  int seed = draft.value("seed", 1337);
  if (ImGui::InputInt(terrainField("Seed").c_str(), &seed))
    draft["seed"] = seed;
  int chunk = draft.value("chunk_cells", 32);
  if (ImGui::InputInt(terrainField("Chunk cells").c_str(), &chunk))
    draft["chunk_cells"] = chunk;
  auto biomes = draft.value("biomes", defaults.at("biomes"));
  const auto previousBiomes = biomes;
  std::string defaultBiome =
      draft.value("default_biome", std::string("default"));
  if (biomeCombo("Default biome", biomes, defaultBiome))
    draft["default_biome"] = defaultBiome;
  ImGui::SeparatorText("Biomes");
  std::optional<std::string> removeBiome;
  std::optional<std::pair<std::string, std::string>> renameBiome;
  for (auto &[id, biome] : biomes.items()) {
    ImGui::PushID(id.c_str());
    if (ImGui::TreeNode(id.c_str())) {
      std::vector<char> idBuffer(id.size() + 256, '\0');
      std::copy(id.begin(), id.end(), idBuffer.begin());
      if (ImGui::InputText(terrainField("Biome ID (Enter)").c_str(),
                           idBuffer.data(), idBuffer.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string replacement(idBuffer.data());
        if (replacement.empty() ||
            (replacement != id && biomes.contains(replacement)))
          notice = "Biome IDs must be nonempty and unique.";
        else if (replacement != id)
          renameBiome = std::pair{id, replacement};
      }
      floatField(biome, "base_height", "Base height", 0, 0.1F, -1e7F, 1e7F);
      floatField(biome, "height_variation", "Height variation", 8, 0.1F, 0,
                 1e7F);
      floatField(biome, "feature_size", "Feature size", 32, 0.1F, 0.01F, 1e7F);
      floatField(biome, "roughness", "Roughness", 0.5F, 0.01F, 0, 1);
      int octaves = biome.value("octaves", 4);
      if (ImGui::InputInt(terrainField("Octaves").c_str(), &octaves))
        biome["octaves"] = octaves;
      auto color = biome.value("color", defaults["biomes"]["default"]["color"]);
      float rgba[]{color.at(0).get<float>(), color.at(1).get<float>(),
                   color.at(2).get<float>(), color.at(3).get<float>()};
      if (ImGui::ColorEdit4(terrainField("Tint").c_str(), rgba))
        biome["color"] = {rgba[0], rgba[1], rgba[2], rgba[3]};
      bool referenced = defaultBiome == id;
      const auto &paintedRegions =
          draft.contains("regions") ? draft.at("regions") : emptyArray;
      for (const auto &region : paintedRegions)
        referenced |= region.value("biome", std::string{}) == id;
      ImGui::BeginDisabled(referenced);
      if (ImGui::SmallButton("Remove biome"))
        removeBiome = id;
      ImGui::EndDisabled();
      if (referenced)
        ImGui::TextWrapped("Used by the default or painted regions.");
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (removeBiome)
    biomes.erase(*removeBiome);
  if (renameBiome) {
    const auto &[previousId, replacementId] = *renameBiome;
    biomes[replacementId] = biomes.at(previousId);
    biomes.erase(previousId);
    if (defaultBiome == previousId) {
      defaultBiome = replacementId;
      draft["default_biome"] = replacementId;
    }
    if (draft.contains("regions")) {
      for (auto &region : draft["regions"])
        if (region.value("biome", std::string{}) == previousId)
          region["biome"] = replacementId;
    }
    if (authoring.brush.biome == previousId)
      authoring.brush.biome = replacementId;
  }
  if (ImGui::Button("Add biome")) {
    int suffix = 1;
    std::string id;
    do {
      id = "biome_" + std::to_string(suffix++);
    } while (biomes.contains(id));
    biomes[id] = defaults["biomes"]["default"];
    authoring.brush.biome = id;
  }
  if (biomes != previousBiomes)
    draft["biomes"] = biomes;
  ImGui::SeparatorText("Edits and protection");
  const auto &edits = draft.contains("edits") ? draft.at("edits") : emptyArray;
  const auto &regions =
      draft.contains("regions") ? draft.at("regions") : emptyArray;
  const bool hasProtection = std::ranges::any_of(edits, [](const auto &edit) {
    return edit.value("type", std::string{}) == "protect";
  });
  ImGui::Text("Draft: %zu edits, %zu painted regions", edits.size(),
              regions.size());
  ImGui::BeginDisabled(!hasProtection);
  if (ImGui::Button("Clear protection"))
    ImGui::OpenPopup("Clear terrain protection?");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(edits.empty() && regions.empty());
  if (ImGui::Button("Clear all edits"))
    ImGui::OpenPopup("Clear all terrain edits?");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  drawClearTerrainDialog("Clear terrain protection?", draft, true);
  drawClearTerrainDialog("Clear all terrain edits?", draft, false);
  if (ImGui::BeginPopupModal("Terrain grid changed", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "Size or resolution changed. Existing radial edits can keep their "
        "local positions. Protection snapshots require the original grid.");
    ImGui::TextWrapped(
        "Clear removes all painted regions, sculpt edits and protection "
        "snapshots. Undo restores the previous recipe.");
    if (ImGui::Button("Keep radial edits")) {
      std::string error;
      if (authoring.generate(EditorTerrainResize::Keep, error))
        ImGui::CloseCurrentPopup();
      else
        notice = std::move(error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear edits and generate")) {
      std::string error;
      if (authoring.generate(EditorTerrainResize::Clear, error))
        ImGui::CloseCurrentPopup();
      else
        notice = std::move(error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  dirty = authoring.hasDraftChanges();
  ImGui::SeparatorText("Viewport brush");
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking() || dirty ||
                       !authoring.surface());
  constexpr std::array names{"Select", "Raise",       "Lower",  "Flatten",
                             "Smooth", "Paint biome", "Protect"};
  int mode = static_cast<int>(authoring.brush.mode);
  if (ImGui::Combo(terrainField("Tool").c_str(), &mode, names.data(),
                   static_cast<int>(names.size())))
    authoring.brush.mode = static_cast<EditorTerrainBrush>(mode);
  ImGui::DragFloat(terrainField("Radius").c_str(), &authoring.brush.radius,
                   0.1F, 0.01F, 1e7F, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  if (authoring.brush.mode != EditorTerrainBrush::Protect) {
    ImGui::SliderFloat(terrainField("Strength").c_str(),
                       &authoring.brush.strength, 0, 1, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat(terrainField("Falloff").c_str(),
                       &authoring.brush.falloff, 0, 4, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
  }
  if (authoring.brush.mode == EditorTerrainBrush::Biome)
    biomeCombo("Paint biome", biomes, authoring.brush.biome);
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Drag on the terrain to paint. Release applies one undoable stroke. Esc "
      "cancels the stroke. Alt+drag navigates.");
  if (authoring.brush.mode == EditorTerrainBrush::Flatten)
    ImGui::TextWrapped("Flatten captures height where the stroke begins.");
  if (authoring.brush.mode == EditorTerrainBrush::Protect)
    ImGui::TextWrapped("Protect captures the current surface so it survives "
                       "later seed and biome changes. Every sample inside the "
                       "radius is captured fully. Clear protection removes "
                       "older snapshots from the draft.");
}
} // namespace demi::editor
