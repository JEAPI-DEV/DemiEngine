#include "editor/EditorTerrainGraphSettings.h"
#include "demi/assets/DataAsset.h"
#include "demi/runtime/terrain/TerrainBrushStroke.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorAssetReferenceControl.h"
#include "editor/EditorColorControl.h"
#include "editor/EditorWorkspace.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <imgui.h>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
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

bool chooseAsset(EditorWorkspace &workspace, const char *label,
                 std::initializer_list<std::string_view> contentTypes,
                 std::string &selected) {
  return drawEditorAssetReferenceControl(
      terrainField(label).c_str(), workspace.assetIndex().registry(),
      [contentTypes](const AssetManifest &manifest) {
        if (manifest.type != "DataAsset")
          return false;
        const auto metadata = assets::dataAssetMetadata(manifest);
        return metadata &&
               std::ranges::find(contentTypes, metadata->contentType) !=
                   contentTypes.end();
      },
      selected);
}

void drawLandforms(nlohmann::json &draft, nlohmann::json &biomes,
                   std::string &notice) {
  const auto defaults = defaultEditorTerrainRecipe();
  auto landforms = draft.value("landforms", defaults.at("landforms"));
  const auto before = landforms;
  std::string defaultLandform =
      draft.value("default_landform", std::string("default"));

  ImGui::TextWrapped("Landforms shape the ground. Biomes refer to them by ID.");
  if (draft.contains("graph") && !draft["graph"].is_null())
    ImGui::TextWrapped("Landform modules use these shapes. Noise and Flat "
                       "modules use their own height parameters.");
  if (ImGui::BeginCombo(terrainField("Default landform").c_str(),
                        defaultLandform.c_str())) {
    for (const auto &[id, shape] : landforms.items()) {
      (void)shape;
      if (ImGui::Selectable(id.c_str(), id == defaultLandform)) {
        defaultLandform = id;
        draft["default_landform"] = id;
      }
    }
    ImGui::EndCombo();
  }

  std::optional<std::pair<std::string, std::string>> rename;
  std::optional<std::string> remove;
  for (auto &[id, shape] : landforms.items()) {
    ImGui::PushID(id.c_str());
    if (ImGui::TreeNode(id.c_str())) {
      std::vector<char> name(id.size() + 256, '\0');
      std::copy(id.begin(), id.end(), name.begin());
      if (ImGui::InputText(terrainField("Landform ID (Enter)").c_str(),
                           name.data(), name.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string replacement(name.data());
        if (replacement.empty() ||
            (replacement != id && landforms.contains(replacement)))
          notice = "Landform IDs must be nonempty and unique.";
        else if (replacement != id)
          rename = std::pair{id, replacement};
      }
      floatField(shape, "base_height", "Base height", 0, 0.1F, -1e7F, 1e7F);
      floatField(shape, "height_variation", "Height variation", 8, 0.1F, 0,
                 1e7F);
      floatField(shape, "feature_size", "Feature size", 32, 0.1F, 0.01F, 1e7F);
      floatField(shape, "roughness", "Roughness", 0.5F, 0.01F, 0, 1);
      int octaves = shape.value("octaves", 4);
      if (ImGui::InputInt(terrainField("Octaves").c_str(), &octaves))
        shape["octaves"] = octaves;

      bool referenced = defaultLandform == id;
      for (const auto &[biomeId, biome] : biomes.items()) {
        (void)biomeId;
        referenced |= biome.value("landform", std::string{}) == id;
      }
      ImGui::BeginDisabled(referenced || landforms.size() <= 1);
      if (ImGui::SmallButton("Remove landform"))
        remove = id;
      ImGui::EndDisabled();
      if (referenced)
        ImGui::TextDisabled("Referenced by the default or a biome.");
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (remove)
    landforms.erase(*remove);
  if (rename) {
    const auto &[previous, replacement] = *rename;
    landforms[replacement] = landforms.at(previous);
    landforms.erase(previous);
    if (defaultLandform == previous)
      draft["default_landform"] = replacement;
    for (auto &[biomeId, biome] : biomes.items()) {
      (void)biomeId;
      if (biome.value("landform", std::string{}) == previous)
        biome["landform"] = replacement;
    }
  }
  if (ImGui::Button("Add landform")) {
    int suffix = 1;
    std::string id;
    do {
      id = "landform_" + std::to_string(suffix++);
    } while (landforms.contains(id));
    landforms[id] = defaults.at("landforms").at("default");
  }
  if (landforms != before)
    draft["landforms"] = std::move(landforms);
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
      draft["exclusions"] = nlohmann::json::array();
    }
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void drawLayers(EditorTerrainAuthoring &authoring) {
  auto &draft = authoring.draft();
  auto layers =
      draft.value("layers", defaultEditorTerrainRecipe().at("layers"));
  const auto before = layers;
  constexpr std::array kinds{"generation", "biome", "sculpt", "protection",
                             "exclusion"};
  constexpr std::array labels{"Generation", "Biome overrides", "Sculpt",
                              "Protection", "Exclusions"};
  std::optional<std::string> removeLayer;
  for (std::size_t group = 0; group < kinds.size(); ++group) {
    ImGui::PushID(kinds[group]);
    if (ImGui::TreeNodeEx(labels[group], ImGuiTreeNodeFlags_DefaultOpen)) {
      for (auto &layer : layers) {
        if (layer.value("kind", std::string{}) != kinds[group])
          continue;
        const auto id = layer.at("id").get<std::string>();
        ImGui::PushID(id.c_str());
        bool enabled = layer.value("enabled", true);
        if (ImGui::Checkbox("##Enabled", &enabled))
          layer["enabled"] = enabled;
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Enable this layer when the draft is applied");
        ImGui::SameLine();
        const auto name = layer.value("name", id);
        std::vector<char> buffer(name.size() + 256, '\0');
        std::copy(name.begin(), name.end(), buffer.begin());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", buffer.data(), buffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue) &&
            buffer.front())
          layer["name"] = buffer.data();
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Layer ID: %s\nEnter to rename", id.c_str());
        if (authoring.brush.layer == id)
          ImGui::TextDisabled("Active brush target");
        bool referenced = authoring.brush.layer == id;
        for (const char *field : {"regions", "edits", "exclusions", "rules"}) {
          if (!draft.contains(field) || !draft[field].is_array())
            continue;
          for (const auto &entry : draft[field])
            referenced |= entry.value("layer", std::string{}) == id;
        }
        // Default IDs are the implicit target for strokes that omit layer.
        const bool isImplicitLayer = id == "generation" || id == "biomes" ||
                                     id == "sculpt" || id == "protection" ||
                                     id == "exclusions";
        ImGui::BeginDisabled(isImplicitLayer || referenced);
        if (ImGui::SmallButton("Remove layer"))
          removeLayer = id;
        ImGui::EndDisabled();
        if (referenced)
          ImGui::TextDisabled(
              "Referenced by a brush or authored terrain data.");
        ImGui::PopID();
      }
      if (group != 0 && ImGui::SmallButton("Add layer")) {
        int suffix = 1;
        std::string id;
        do {
          id = std::string(kinds[group]) + "_" + std::to_string(suffix++);
        } while (std::ranges::any_of(layers, [&](const auto &entry) {
          return entry.value("id", std::string{}) == id;
        }));
        layers.push_back({{"id", id},
                          {"name", labels[group]},
                          {"kind", kinds[group]},
                          {"enabled", true}});
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (removeLayer) {
    for (auto layer = layers.begin(); layer != layers.end(); ++layer) {
      if (layer->value("id", std::string{}) == *removeLayer) {
        layers.erase(layer);
        break;
      }
    }
  }
  if (layers != before)
    draft["layers"] = std::move(layers);
  ImGui::TextWrapped("Names and layer toggles are draft settings. Regenerate "
                     "applies them; disabled layers keep their strokes.");
}

bool biomeRuleLayerCombo(const char *label, const nlohmann::json &layers,
                         std::string &selected) {
  bool changed = false;
  if (ImGui::BeginCombo(terrainField(label).c_str(),
                        selected.empty() ? "Choose layer" : selected.c_str())) {
    for (const auto &layer : layers) {
      if (layer.value("kind", std::string{}) != "biome")
        continue;
      const auto id = layer.at("id").get<std::string>();
      const bool enabled = layer.value("enabled", true);
      ImGui::PushID(id.c_str());
      ImGui::BeginDisabled(!enabled);
      if (ImGui::Selectable(layer.value("name", id).c_str(), id == selected)) {
        selected = id;
        changed = true;
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  return changed;
}

// An unchecked band removes its key. Omitted and null both mean unconstrained,
// so writing null would survive parsing but blur the authored intent.
void drawRuleBand(nlohmann::json &rule, const char *key, const char *label) {
  ImGui::PushID(key);
  bool enabled = rule.contains(key) && !rule.at(key).is_null();
  float bounds[]{0.0F, 0.0F};
  if (enabled) {
    const auto &band = rule.at(key);
    bounds[0] = band.at(0).get<float>();
    bounds[1] = band.at(1).get<float>();
  }
  const bool toggled = ImGui::Checkbox("##enabled", &enabled);
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
  if (toggled) {
    if (enabled)
      rule[key] = {bounds[0], bounds[1]};
    else
      rule.erase(key);
    ImGui::PopID();
    return;
  }
  if (!enabled) {
    ImGui::PopID();
    return;
  }
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::DragFloat2("##bounds", bounds, 0.01F, -1e7F, 1e7F, "%.3f",
                        ImGuiSliderFlags_AlwaysClamp))
    rule[key] = {bounds[0], bounds[1]};
  ImGui::PopID();
}

void drawRuleSubstrate(nlohmann::json &rule) {
  static constexpr std::array kinds{
      runtime::TerrainSubstrate::Soil, runtime::TerrainSubstrate::Rock,
      runtime::TerrainSubstrate::Sand, runtime::TerrainSubstrate::Wet};
  unsigned mask = 0;
  for (const auto &entry : rule.value("substrate", nlohmann::json::array())) {
    if (const auto substrate =
            runtime::terrainSubstrateFromName(entry.get<std::string>()))
      mask |= 1U << static_cast<unsigned>(std::distance(
                  kinds.begin(), std::ranges::find(kinds, *substrate)));
  }
  const auto preview = mask == 0
                           ? std::string("Any substrate")
                           : std::to_string(std::popcount(mask)) + " selected";
  bool changed = false;
  if (ImGui::BeginCombo(terrainField("Substrate").c_str(), preview.c_str())) {
    for (std::size_t index = 0; index < kinds.size(); ++index) {
      const auto name =
          std::string(runtime::terrainSubstrateName(kinds[index]));
      if (ImGui::Selectable(name.c_str(), (mask & (1U << index)) != 0)) {
        mask ^= 1U << index;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  if (!changed)
    return;
  if (mask == 0) {
    rule.erase("substrate");
    return;
  }
  // Substrate order carries no meaning, so store the enum order and keep
  // cosmetic toggles from churning the authored file.
  nlohmann::json names = nlohmann::json::array();
  for (std::size_t index = 0; index < kinds.size(); ++index)
    if ((mask & (1U << index)) != 0)
      names.push_back(runtime::terrainSubstrateName(kinds[index]));
  rule["substrate"] = std::move(names);
}

void drawRules(EditorTerrainAuthoring &authoring, std::string &notice) {
  auto &draft = authoring.draft();
  const auto defaults = defaultEditorTerrainRecipe();
  auto rules = draft.value("rules", defaults.at("rules"));
  const auto previousRules = rules;
  const auto biomes = draft.value("biomes", defaults.at("biomes"));
  const auto layers = draft.value("layers", defaults.at("layers"));
  const auto defaultBiome =
      draft.value("default_biome", std::string("default"));
  ImGui::TextWrapped("Rules assign biomes automatically from field conditions. "
                     "They are draft settings: Regenerate applies them.");
  std::optional<std::size_t> removeRule;
  std::optional<std::pair<std::size_t, std::string>> renameRule;
  for (std::size_t index = 0; index < rules.size(); ++index) {
    auto &rule = rules[index];
    const auto id = rule.value("id", std::string("rule"));
    ImGui::PushID(static_cast<int>(index));
    if (ImGui::TreeNode(id.c_str(), "%s", id.c_str())) {
      std::vector<char> idBuffer(id.size() + 256, '\0');
      std::copy(id.begin(), id.end(), idBuffer.begin());
      if (ImGui::InputText(terrainField("Rule ID (Enter)").c_str(),
                           idBuffer.data(), idBuffer.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string replacement(idBuffer.data());
        if (replacement.empty())
          notice = "Rule IDs must be nonempty.";
        else if (replacement != id)
          renameRule = std::pair{index, replacement};
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Enter to rename. Nothing else references this id.");
      std::string biome = rule.value("biome", defaultBiome);
      if (biomeCombo("Target biome", biomes, biome))
        rule["biome"] = biome;
      std::string layer = rule.value("layer", std::string("biomes"));
      if (biomeRuleLayerCombo("Target layer", layers, layer))
        rule["layer"] = layer;
      const bool evaluated =
          std::ranges::any_of(layers, [&](const auto &entry) {
            return entry.value("kind", std::string{}) == "biome" &&
                   entry.value("enabled", true) &&
                   entry.value("id", std::string{}) == layer;
          });
      if (!evaluated)
        ImGui::TextDisabled("Only rules on an enabled biome layer are "
                            "evaluated.");
      int priority = rule.value("priority", 0);
      if (ImGui::InputInt(terrainField("Priority").c_str(), &priority))
        rule["priority"] = priority;
      float blend = rule.value("blend", 0.0F);
      if (ImGui::DragFloat(terrainField("Blend").c_str(), &blend, 0.01F, 0.0F,
                           1e3F, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        rule["blend"] = blend;
      drawRuleBand(rule, "elevation", "Elevation band");
      drawRuleBand(rule, "slope", "Slope band");
      drawRuleBand(rule, "moisture", "Moisture band");
      drawRuleBand(rule, "water_distance", "Water distance band");
      drawRuleSubstrate(rule);
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove rule"))
        removeRule = index;
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Add rule")) {
    int suffix = 1;
    std::string id;
    do {
      id = "rule_" + std::to_string(suffix++);
    } while (std::ranges::any_of(rules, [&](const auto &entry) {
      return entry.value("id", std::string{}) == id;
    }));
    nlohmann::json rule{{"id", id}, {"priority", 0}};
    rule["biome"] =
        biomes.contains(defaultBiome)
            ? defaultBiome
            : (biomes.empty() ? defaultBiome : biomes.begin().key());
    rule["layer"] = "biomes";
    for (const auto &entry : layers) {
      if (entry.value("kind", std::string{}) == "biome" &&
          entry.value("enabled", true)) {
        rule["layer"] = entry.at("id").template get<std::string>();
        break;
      }
    }
    rules.push_back(std::move(rule));
  }
  if (removeRule) {
    rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(*removeRule));
    renameRule.reset();
  }
  if (renameRule) {
    const auto &[index, replacement] = *renameRule;
    const bool duplicate = std::ranges::any_of(rules, [&](const auto &entry) {
      return entry.value("id", std::string{}) == replacement;
    });
    if (duplicate)
      notice = "Rule IDs must be unique.";
    else
      rules[index]["id"] = replacement;
  }
  if (rules != previousRules)
    draft["rules"] = std::move(rules);
}

} // namespace

namespace {

void drawGrid(EditorTerrainAuthoring &authoring) {
  auto &draft = authoring.draft();
  const auto defaults = defaultEditorTerrainRecipe();
  ImGui::TextWrapped("World dimensions, sample grid and seed are shared by all "
                     "graph nodes. Generate applies grid changes.");
  auto size = draft.value("size", defaults.at("size"));
  float dimensions[]{size.at(0).get<float>(), size.at(1).get<float>()};
  if (ImGui::DragFloat2(terrainField("Size (X/Z)").c_str(), dimensions, 1.0F,
                        0.01F, 1e7F, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    draft["size"] = {dimensions[0], dimensions[1]};
  auto resolution = draft.value("resolution", defaults.at("resolution"));
  int cells[]{resolution.at(0).get<int>(), resolution.at(1).get<int>()};
  if (ImGui::InputInt2(terrainField("Resolution (cells)").c_str(), cells))
    draft["resolution"] = {cells[0], cells[1]};
  int seed = draft.value("seed", defaults.at("seed").get<int>());
  if (ImGui::InputInt(terrainField("Seed").c_str(), &seed))
    draft["seed"] = seed;
  int chunk = draft.value("chunk_cells", defaults.at("chunk_cells").get<int>());
  if (ImGui::InputInt(terrainField("Chunk cells").c_str(), &chunk))
    draft["chunk_cells"] = chunk;
}

void drawBiomeColor(nlohmann::json &biome) {
  const auto nativeDefault = runtime::TerrainBiome{}.color;
  const auto color =
      biome.value("color", nlohmann::json{nativeDefault.r, nativeDefault.g,
                                          nativeDefault.b, nativeDefault.a});
  float rgba[]{color.at(0).get<float>(), color.at(1).get<float>(),
               color.at(2).get<float>(), color.at(3).get<float>()};
  constexpr ImGuiColorEditFlags flags =
      ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB |
      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf |
      ImGuiColorEditFlags_AlphaBar;
  const bool changed = drawEditorColorControl(
      terrainField("Color (Hex RGBA)").c_str(), rgba,
      {.flags = flags});
  if (changed)
    biome["color"] = {rgba[0], rgba[1], rgba[2], rgba[3]};
}

void drawBiomeTextureScale(nlohmann::json &biome, std::string &notice) {
  const float fallback = runtime::TerrainBiome{}.textureScale;
  float scale = biome.value("texture_scale", fallback);
  const std::string label =
      terrainField("Texture scale (repeats/local unit, Enter)");
  ImGui::SetNextItemWidth(-105.0F);
  if (ImGui::InputFloat(label.c_str(), &scale, 0.0F, 0.0F, "%.9g",
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (!std::isfinite(scale) || scale <= 0.0F)
      notice = "Texture scale must be a positive finite number.";
    else if (scale == fallback)
      biome.erase("texture_scale");
    else
      biome["texture_scale"] = scale;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!biome.contains("texture_scale"));
  if (ImGui::SmallButton("Reset scale"))
    biome.erase("texture_scale");
  ImGui::EndDisabled();
  ImGui::TextDisabled("Albedo repeats per terrain-local unit; default %.0f.",
                      fallback);
}

void drawAppearance(EditorWorkspace &workspace, std::string &notice) {
  auto &authoring = workspace.terrainAuthoring();
  auto &draft = authoring.draft();
  static const nlohmann::json emptyArray = nlohmann::json::array();
  const auto defaults = defaultEditorTerrainRecipe();
  auto biomes = draft.value("biomes", defaults.at("biomes"));
  const auto previousBiomes = biomes;
  std::string defaultBiome =
      draft.value("default_biome", std::string("default"));
  ImGui::TextWrapped("Biome tint: Hex RGBA or precise normalized channels.");
  ImGui::TextWrapped("Ordinary Material values and tiled albedo reach terrain. "
                     "Smooth blending and typed terrain Material PBR maps are "
                     "pending. Water appearance is configured on Water Body nodes.");
  if (biomeCombo("Default biome", biomes, defaultBiome))
    draft["default_biome"] = defaultBiome;
  std::optional<std::string> removeBiome;
  std::optional<std::pair<std::string, std::string>> renameBiome;
  for (auto &[id, biome] : biomes.items()) {
    ImGui::PushID(id.c_str());
    ImGui::SeparatorText(id.c_str());
    drawBiomeColor(biome);
    if (ImGui::TreeNode("Biome properties")) {
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
      const auto landforms = draft.value("landforms", defaults.at("landforms"));
      std::string shape = biome.value("landform", std::string{});
      const std::string shapePreview =
          shape.empty() ? "Default landform" : shape;
      if (ImGui::BeginCombo(terrainField("Landform").c_str(),
                            shapePreview.c_str())) {
        if (ImGui::Selectable("Default landform", shape.empty()))
          biome.erase("landform");
        for (const auto &[landformId, landform] : landforms.items()) {
          (void)landform;
          if (ImGui::Selectable(landformId.c_str(), shape == landformId))
            biome["landform"] = landformId;
        }
        ImGui::EndCombo();
      }
      std::string material = biome.value("material", std::string{});
      if (drawEditorAssetReferenceControl(
              terrainField("Render material (Material)").c_str(),
              workspace.assetIndex().registry(),
              [](const AssetManifest &manifest) {
                return manifest.type == "Material";
              },
              material)) {
        if (material.empty())
          biome.erase("material");
        else
          biome["material"] = material;
      }
      drawBiomeTextureScale(biome, notice);
      ImGui::TextDisabled("Terrain Material/Set definitions remain editable "
                          "in Assets; terrain PBR shading is pending.");
      bool referenced = defaultBiome == id || biomes.size() <= 1;
      const auto &paintedRegions =
          draft.contains("regions") ? draft.at("regions") : emptyArray;
      for (const auto &region : paintedRegions)
        referenced |= region.value("biome", std::string{}) == id;
      const auto &rules =
          draft.contains("rules") ? draft.at("rules") : emptyArray;
      for (const auto &rule : rules)
        referenced |= rule.value("biome", std::string{}) == id;
      ImGui::BeginDisabled(referenced);
      if (ImGui::SmallButton("Remove biome"))
        removeBiome = id;
      ImGui::EndDisabled();
      if (referenced)
        ImGui::TextWrapped("Referenced by the default, a rule or a painted "
                           "region.");
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
    if (draft.contains("rules")) {
      for (auto &rule : draft["rules"])
        if (rule.value("biome", std::string{}) == previousId)
          rule["biome"] = replacementId;
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
}

void drawPlacement(EditorWorkspace &workspace) {
  auto &draft = workspace.terrainAuthoring().draft();
  std::string palette = draft.value("palette", std::string{});
  if (chooseAsset(workspace, "Scatter palette asset", {"terrain_palette"},
                  palette)) {
    if (palette.empty())
      draft.erase("palette");
    else
      draft["palette"] = palette;
  }
  if (draft.contains("graph") && !draft["graph"].is_null())
    ImGui::TextWrapped("Connect Scatter Palette to Terrain Output's Instances "
                       "input to place this palette. Connect Biome Rules "
                       "upstream when rules should guide placement.");
  ImGui::TextWrapped("Density, asset choices and placement constraints are "
                     "authored in the selected terrain_palette asset. "
                     "No palette means no scatter placements.");
}

void drawEdits(EditorTerrainAuthoring &authoring) {
  auto &draft = authoring.draft();
  static const nlohmann::json emptyArray = nlohmann::json::array();
  const auto &edits = draft.contains("edits") ? draft.at("edits") : emptyArray;
  const auto &regions =
      draft.contains("regions") ? draft.at("regions") : emptyArray;
  const bool hasProtection = std::ranges::any_of(edits, [](const auto &edit) {
    return edit.value("type", std::string{}) == "protect";
  });
  ImGui::Text("Draft: %zu edits, %zu painted regions", edits.size(),
              regions.size());
  if (ImGui::Button("Compact brush data")) {
    draft = runtime::compactTerrainBrushRecipe(draft);
    return;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Store shared brush settings once with ordered points. "
                      "No stamps are removed. Generate applies this draft.");
  ImGui::BeginDisabled(!hasProtection);
  if (ImGui::Button("Clear protection"))
    ImGui::OpenPopup("Clear terrain protection?");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(edits.empty() && regions.empty());
  if (ImGui::Button("Clear all edits"))
    ImGui::OpenPopup("Clear all terrain edits?");
  ImGui::EndDisabled();
  drawClearTerrainDialog("Clear terrain protection?", draft, true);
  drawClearTerrainDialog("Clear all terrain edits?", draft, false);
}

void drawLandformSettings(EditorTerrainAuthoring &authoring,
                          std::string &notice) {
  auto &draft = authoring.draft();
  auto biomes =
      draft.value("biomes", defaultEditorTerrainRecipe().at("biomes"));
  const auto before = biomes;
  drawLandforms(draft, biomes, notice);
  if (biomes != before)
    draft["biomes"] = std::move(biomes);
}

void drawOutputSummary(const EditorTerrainAuthoring &authoring) {
  const auto &draft = authoring.draft();
  const auto defaults = defaultEditorTerrainRecipe();
  const auto size = draft.value("size", defaults.at("size"));
  const auto cells = draft.value("resolution", defaults.at("resolution"));
  ImGui::TextWrapped("Surface: %.2f x %.2f world units, %d x %d cells.",
                     size.at(0).get<float>(), size.at(1).get<float>(),
                     cells.at(0).get<int>(), cells.at(1).get<int>());
  ImGui::TextWrapped("Output publishes the connected field, then replays "
                     "painted biomes, sculpt edits and protection. "
                     "Connect optional Water and Instances inputs to retain "
                     "those results.");
}

} // namespace

void drawTerrainGraphSettings(EditorWorkspace &workspace, std::string &notice) {
  auto &authoring = workspace.terrainAuthoring();
  if (authoring.entityId().empty()) {
    ImGui::TextWrapped(
        "Select a configured Terrain3D entity to edit its generation recipe.");
    return;
  }
  ImGui::TextWrapped(
      "Shared terrain draft. Generate applies changes to the preview.");
  ImGui::TextWrapped(
      workspace.terrainEditingAsset()
          ? "Apply changes to asset saves its source."
          : "Save persists the generated procedural recipe in its document.");
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  if (ImGui::CollapsingHeader("Appearance - biome surfaces",
                              ImGuiTreeNodeFlags_DefaultOpen))
    drawAppearance(workspace, notice);
  if (ImGui::CollapsingHeader("Terrain grid and seed"))
    drawGrid(authoring);
  if (ImGui::CollapsingHeader("Landforms"))
    drawLandformSettings(authoring, notice);
  if (ImGui::CollapsingHeader("Biome rules"))
    drawRules(authoring, notice);
  if (ImGui::CollapsingHeader("Placement - scatter palette"))
    drawPlacement(workspace);
  if (ImGui::CollapsingHeader("Layers"))
    drawLayers(authoring);
  if (ImGui::CollapsingHeader("Edits and protection"))
    drawEdits(authoring);
  ImGui::EndDisabled();
}

void drawTerrainNodeSettings(EditorWorkspace &workspace,
                             const std::string_view nodeType,
                             std::string &notice) {
  if (nodeType != "landform" && nodeType != "biomes" && nodeType != "scatter" &&
      nodeType != "output")
    return;
  auto &authoring = workspace.terrainAuthoring();
  if (authoring.entityId().empty()) {
    ImGui::TextWrapped("Open a configured terrain to edit these settings.");
    return;
  }
  ImGui::TextWrapped(
      "Shared landscape settings. Generate updates the preview.");
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  if (nodeType == "landform") {
    if (ImGui::CollapsingHeader("Landforms", ImGuiTreeNodeFlags_DefaultOpen))
      drawLandformSettings(authoring, notice);
  } else if (nodeType == "biomes") {
    if (ImGui::CollapsingHeader("Appearance - biome surfaces",
                                ImGuiTreeNodeFlags_DefaultOpen))
      drawAppearance(workspace, notice);
    if (ImGui::CollapsingHeader("Biome rules", ImGuiTreeNodeFlags_DefaultOpen))
      drawRules(authoring, notice);
  } else if (nodeType == "scatter") {
    if (ImGui::CollapsingHeader("Placement - scatter palette",
                                ImGuiTreeNodeFlags_DefaultOpen))
      drawPlacement(workspace);
  } else {
    drawOutputSummary(authoring);
    if (ImGui::CollapsingHeader("Appearance - biome surfaces",
                                ImGuiTreeNodeFlags_DefaultOpen))
      drawAppearance(workspace, notice);
    if (ImGui::CollapsingHeader("Terrain grid and seed"))
      drawGrid(authoring);
    if (ImGui::CollapsingHeader("Layers"))
      drawLayers(authoring);
    if (ImGui::CollapsingHeader("Edits and protection"))
      drawEdits(authoring);
  }
  ImGui::EndDisabled();
}

} // namespace demi::editor
