#include "editor/EditorDataAssetControls.h"

#include "editor/EditorAssetReferenceControl.h"
#include "editor/EditorColorControl.h"
#include "editor/EditorJsonDocument.h"
#include "editor/EditorWorkspace.h"

#include "demi/assets/DataAsset.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/assets/MaterialSet.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

constexpr assets::TerrainMaterialMapSlot MapSlots[] = {
    assets::TerrainMaterialMapSlot::BaseColor,
    assets::TerrainMaterialMapSlot::Normal,
    assets::TerrainMaterialMapSlot::Roughness,
    assets::TerrainMaterialMapSlot::Metallic,
    assets::TerrainMaterialMapSlot::AmbientOcclusion,
    assets::TerrainMaterialMapSlot::Height,
    assets::TerrainMaterialMapSlot::Detail,
    assets::TerrainMaterialMapSlot::Emissive};

constexpr assets::TerrainMaterialRole MaterialRoles[] = {
    assets::TerrainMaterialRole::Rock,
    assets::TerrainMaterialRole::Cliff,
    assets::TerrainMaterialRole::Sediment,
    assets::TerrainMaterialRole::Ground,
    assets::TerrainMaterialRole::Grass,
    assets::TerrainMaterialRole::Sand,
    assets::TerrainMaterialRole::Snow,
    assets::TerrainMaterialRole::WetGround,
    assets::TerrainMaterialRole::Underwater};

constexpr runtime::TerrainPaletteRole PaletteRoles[] = {
    runtime::TerrainPaletteRole::ExposedRock,
    runtime::TerrainPaletteRole::Soil,
    runtime::TerrainPaletteRole::Sand,
    runtime::TerrainPaletteRole::Snow,
    runtime::TerrainPaletteRole::WetGround,
    runtime::TerrainPaletteRole::Tree,
    runtime::TerrainPaletteRole::Bush,
    runtime::TerrainPaletteRole::Grass,
    runtime::TerrainPaletteRole::Reed,
    runtime::TerrainPaletteRole::CliffPiece,
    runtime::TerrainPaletteRole::Debris};

bool commit(EditorJsonDocument &document, const std::string &pointer,
            Json value, std::string &notice) {
  std::string error;
  if (!document.set(pointer, std::move(value), error)) {
    notice = error;
    return false;
  }
  notice = "Data asset updated";
  return true;
}

bool commitRoot(EditorJsonDocument &document, Json value, std::string &notice) {
  std::string error;
  if (!document.replace(std::move(value), error)) {
    notice = error;
    return false;
  }
  notice = "Data asset updated";
  return true;
}

bool editText(const char *label, const std::string &current,
              std::string &replacement) {
  std::vector<char> buffer(std::max<std::size_t>(current.size() + 1, 512), 0);
  std::memcpy(buffer.data(), current.data(), current.size());
  if (!ImGui::InputText(label, buffer.data(), buffer.size(),
                        ImGuiInputTextFlags_EnterReturnsTrue))
    return false;
  replacement = buffer.data();
  return replacement != current;
}

void editHeader(EditorJsonDocument &document, bool hasDescription,
                std::string &notice) {
  const Json snapshot = document.json();
  std::string replacement;
  if (editText("Name (Enter)", snapshot.value("name", ""), replacement)) {
    (void)commit(document, "/name", replacement, notice);
    return;
  }
  if (hasDescription &&
      editText("Description (Enter)", snapshot.value("description", ""),
               replacement))
    (void)commit(document, "/description", replacement, notice);
}

bool isMaterial(const AssetManifest &manifest) {
  if (manifest.type == "Material")
    return true;
  if (manifest.type != "DataAsset")
    return false;
  const auto metadata = assets::dataAssetMetadata(manifest);
  return metadata && metadata->contentType == "terrain_material";
}

bool chooseAsset(const char *label, EditorWorkspace &workspace,
                 const EditorAssetPredicate &accepts, std::string &value,
                 bool allowNone = true) {
  return drawEditorAssetReferenceControl(
      label, workspace.assetIndex().registry(), accepts, value, allowNone);
}

std::vector<std::string> prefabReferences(const EditorWorkspace &workspace) {
  std::vector<std::string> references;
  const auto &registry = workspace.assetIndex().registry();
  const std::filesystem::path prefabRoot =
      registry.projectDirectory / "prefabs";
  for (const auto &source : collectProjectSourceFiles(registry)) {
    const auto relative = source.lexically_relative(prefabRoot);
    const std::string name = relative.generic_string();
    constexpr std::string_view suffix = ".prefab.json";
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == ".." || !name.ends_with(suffix))
      continue;
    references.push_back("prefab://" +
                         name.substr(0, name.size() - suffix.size()));
  }
  std::sort(references.begin(), references.end());
  references.erase(std::unique(references.begin(), references.end()),
                   references.end());
  return references;
}

bool choosePrefab(const char *label, const EditorWorkspace &workspace,
                  std::string &selected) {
  if (!ImGui::BeginCombo(label, selected.empty() ? "None" : selected.c_str()))
    return false;
  bool changed = false;
  if (ImGui::Selectable("None", selected.empty())) {
    changed = !selected.empty();
    selected.clear();
  }
  for (const std::string &reference : prefabReferences(workspace)) {
    if (ImGui::Selectable(reference.c_str(), selected == reference)) {
      changed = selected != reference;
      selected = reference;
    }
  }
  ImGui::EndCombo();
  return changed;
}

bool editFloat(const char *label, float current, float minimum, float maximum,
               float &replacement) {
  replacement = current;
  return ImGui::DragFloat(label, &replacement, 0.01F, minimum, maximum, "%.3f",
                          ImGuiSliderFlags_AlwaysClamp |
                              ImGuiSliderFlags_NoRoundToFormat);
}

void drawMaterial(EditorWorkspace &workspace, EditorJsonDocument &document,
                  std::string &notice) {
  editHeader(document, false, notice);
  const Json snapshot = document.json();
  const auto scalar = [&](const char *key, float fallback, float minimum,
                          float maximum) {
    float replacement;
    if (editFloat(key, snapshot.value(key, fallback), minimum, maximum,
                  replacement))
      (void)commit(document, "/" + std::string(key), replacement, notice);
  };

  std::array<float, 4> color{0.8F, 0.8F, 0.8F, 1.0F};
  if (snapshot.contains("base_color"))
    for (std::size_t index = 0; index < color.size(); ++index)
      color[index] = snapshot.at("base_color").at(index).get<float>();
  if (drawEditorColorControl("Base color", color.data()))
    (void)commit(document, "/base_color", color, notice);

  scalar("roughness", 0.8F, 0.F, 1.F);
  scalar("metallic", 0.F, 0.F, 1.F);
  scalar("normal_strength", 1.F, 0.F, 4.F);
  scalar("tiling", 1.F, std::numeric_limits<float>::denorm_min(),
         std::numeric_limits<float>::max());
  bool triplanar = snapshot.value("triplanar", false);
  if (ImGui::Checkbox("Triplanar", &triplanar))
    (void)commit(document, "/triplanar", triplanar, notice);

  Json maps = snapshot.value("maps", Json::array());
  ImGui::SeparatorText("Texture maps");
  for (const auto slot : MapSlots) {
    const std::string slotName(assets::terrainMaterialMapSlotName(slot));
    std::string reference;
    std::size_t entryIndex = maps.size();
    for (std::size_t index = 0; index < maps.size(); ++index)
      if (maps[index].contains(slotName)) {
        reference = maps[index].at(slotName).get<std::string>();
        entryIndex = index;
        break;
      }
    if (!chooseAsset(
            slotName.c_str(), workspace,
            [](const AssetManifest &asset) {
              return asset.type == "Texture2D";
            },
            reference))
      continue;
    if (reference.empty() && entryIndex < maps.size())
      maps.erase(maps.begin() + static_cast<Json::difference_type>(entryIndex));
    else if (!reference.empty() && entryIndex < maps.size())
      maps[entryIndex][slotName] = reference;
    else if (!reference.empty())
      maps.push_back({{slotName, reference}});
    if (slot == assets::TerrainMaterialMapSlot::Detail && reference.empty() &&
        snapshot.value("detail_strength", 0.F) > 0.F) {
      Json replacement = snapshot;
      replacement["maps"] = maps;
      replacement["detail_strength"] = 0.F;
      (void)commitRoot(document, std::move(replacement), notice);
    } else {
      (void)commit(document, "/maps", maps, notice);
    }
    return;
  }
  const bool hasDetail =
      std::any_of(maps.begin(), maps.end(),
                  [](const Json &entry) { return entry.contains("detail"); });
  ImGui::BeginDisabled(!hasDetail);
  scalar("detail_strength", 0.F, 0.F, std::numeric_limits<float>::max());
  ImGui::EndDisabled();
  if (!hasDetail)
    ImGui::TextDisabled("Assign a detail texture to enable detail strength.");
}

void toggleRequired(EditorJsonDocument &document, const Json &snapshot,
                    const std::string &role, std::string &notice) {
  Json required = snapshot.value("required_roles", Json::array());
  const auto found = std::find(required.begin(), required.end(), role);
  if (found == required.end())
    required.push_back(role);
  else
    required.erase(found);
  (void)commit(document, "/required_roles", required, notice);
}

void removeRole(EditorJsonDocument &document, const Json &snapshot,
                const std::string &role, std::string &notice) {
  Json replacement = snapshot;
  replacement["roles"].erase(role);
  if (replacement.contains("required_roles")) {
    Json &required = replacement["required_roles"];
    required.erase(std::remove(required.begin(), required.end(), role),
                   required.end());
  }
  (void)commitRoot(document, std::move(replacement), notice);
}

template <typename Role, std::size_t Count, typename Name>
void drawRequiredRoleList(EditorJsonDocument &document, const Json &snapshot,
                          const Role (&roles)[Count], Name name,
                          std::string &notice) {
  const Json authored = snapshot.value("roles", Json::object());
  const Json required = snapshot.value("required_roles", Json::array());
  ImGui::SeparatorText("Required roles");
  for (const Role role : roles) {
    const std::string key(name(role));
    if (!authored.contains(key))
      continue;
    bool checked =
        std::find(required.begin(), required.end(), key) != required.end();
    if (ImGui::Checkbox(key.c_str(), &checked)) {
      toggleRequired(document, snapshot, key, notice);
      return;
    }
  }
}

void drawMaterialSet(EditorWorkspace &workspace, EditorJsonDocument &document,
                     std::string &notice) {
  editHeader(document, true, notice);
  const Json snapshot = document.json();
  const Json roles = snapshot.value("roles", Json::object());
  ImGui::SeparatorText("Material roles");
  for (const auto role : MaterialRoles) {
    const std::string key(assets::terrainMaterialRoleName(role));
    ImGui::PushID(key.c_str());
    std::string selected = roles.value(key, "");
    if (chooseAsset(key.c_str(), workspace, isMaterial, selected, false)) {
      (void)commit(document, "/roles/" + key, selected, notice);
      ImGui::PopID();
      return;
    }
    if (roles.contains(key)) {
      ImGui::SameLine();
      ImGui::BeginDisabled(roles.size() <= 1);
      if (ImGui::SmallButton("Remove")) {
        removeRole(document, snapshot, key, notice);
        ImGui::EndDisabled();
        ImGui::PopID();
        return;
      }
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }
  drawRequiredRoleList(document, snapshot, MaterialRoles,
                       assets::terrainMaterialRoleName, notice);
}

void editPaletteRole(EditorWorkspace &workspace, EditorJsonDocument &document,
                     const std::string &key, const Json &snapshot,
                     std::string &notice) {
  const Json roles = snapshot.value("roles", Json::object());
  const bool exists = roles.contains(key);
  Json entry = exists ? roles.at(key) : Json::object();
  ImGui::PushID(key.c_str());
  if (!ImGui::TreeNodeEx(key.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PopID();
    return;
  }

  std::string asset = entry.value("asset", "");
  const std::string prefab = entry.value("prefab", "");
  if (chooseAsset(
          "Asset", workspace,
          [&](const AssetManifest &manifest) {
            return !prefab.empty() || manifest.type == "Model3D";
          },
          asset, false)) {
    entry["asset"] = asset;
    (void)commit(document, "/roles/" + key, entry, notice);
    ImGui::TreePop();
    ImGui::PopID();
    return;
  }
  if (!exists)
    ImGui::TextDisabled("Choose a Model3D asset to add this role.");
  if (exists) {
    std::string selectedPrefab = prefab;
    if (choosePrefab("Prefab", workspace, selectedPrefab)) {
      if (selectedPrefab.empty())
        entry.erase("prefab");
      else
        entry["prefab"] = selectedPrefab;
      (void)commit(document, "/roles/" + key, entry, notice);
      ImGui::TreePop();
      ImGui::PopID();
      return;
    }
    const auto floatEntry = [&](const char *field, float fallback,
                                float minimum, float maximum) {
      float replacement;
      if (!editFloat(field, entry.value(field, fallback), minimum, maximum,
                     replacement))
        return false;
      entry[field] = replacement;
      (void)commit(document, "/roles/" + key, entry, notice);
      return true;
    };
    if (floatEntry("weight", 1.F, 0.F, std::numeric_limits<float>::max()) ||
        floatEntry("spacing", 1.F, 0.F, std::numeric_limits<float>::max())) {
      ImGui::TreePop();
      ImGui::PopID();
      return;
    }
    Json scale = entry.value("scale", Json::array({1.F, 1.F}));
    float minimum = scale[0].get<float>();
    float maximum = scale[1].get<float>();
    bool scaleChanged = editFloat("Scale minimum", minimum,
                                  std::numeric_limits<float>::denorm_min(),
                                  std::numeric_limits<float>::max(), minimum);
    scaleChanged |= editFloat("Scale maximum", maximum,
                              std::numeric_limits<float>::denorm_min(),
                              std::numeric_limits<float>::max(), maximum);
    if (scaleChanged) {
      if (minimum > maximum)
        notice = "Scale minimum must not exceed maximum.";
      else {
        entry["scale"] = Json::array({minimum, maximum});
        (void)commit(document, "/roles/" + key, entry, notice);
      }
      ImGui::TreePop();
      ImGui::PopID();
      return;
    }
    std::string collision = entry.value("collision", "static");
    if (ImGui::BeginCombo("Collision", collision.c_str())) {
      for (const char *choice : {"none", "static", "trigger"})
        if (ImGui::Selectable(choice, collision == choice)) {
          entry["collision"] = choice;
          (void)commit(document, "/roles/" + key, entry, notice);
        }
      ImGui::EndCombo();
    }
    int lod = entry.value("lod", 0);
    if (ImGui::DragInt("LOD", &lod, 1.F, 0, std::numeric_limits<int>::max(),
                       "%d", ImGuiSliderFlags_AlwaysClamp)) {
      entry["lod"] = lod;
      (void)commit(document, "/roles/" + key, entry, notice);
    }
    const Json biomes = entry.value("biomes", Json::array());
    std::string biome;
    if (editText("Add biome (Enter)", "", biome) && !biome.empty()) {
      Json replacement = biomes;
      if (std::find(replacement.begin(), replacement.end(), biome) ==
          replacement.end())
        replacement.push_back(biome);
      entry["biomes"] = replacement;
      (void)commit(document, "/roles/" + key, entry, notice);
    }
    for (const std::string name : biomes.get<std::vector<std::string>>()) {
      ImGui::PushID(name.c_str());
      ImGui::TextUnformatted(name.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove biome")) {
        Json replacement = biomes;
        replacement.erase(
            std::remove(replacement.begin(), replacement.end(), name),
            replacement.end());
        entry["biomes"] = replacement;
        (void)commit(document, "/roles/" + key, entry, notice);
        ImGui::PopID();
        ImGui::TreePop();
        ImGui::PopID();
        return;
      }
      ImGui::PopID();
    }
    ImGui::BeginDisabled(roles.size() <= 1);
    if (ImGui::SmallButton("Remove role"))
      removeRole(document, snapshot, key, notice);
    ImGui::EndDisabled();
  }
  ImGui::TreePop();
  ImGui::PopID();
}

void drawPalette(EditorWorkspace &workspace, EditorJsonDocument &document,
                 std::string &notice) {
  editHeader(document, false, notice);
  const Json snapshot = document.json();
  ImGui::SeparatorText("Palette roles");
  ImGui::TextDisabled("Biome names are free text and match names in the "
                      "terrain recipe that uses this palette.");
  for (const auto role : PaletteRoles) {
    const std::string key(runtime::terrainPaletteRoleName(role));
    editPaletteRole(workspace, document, key, snapshot, notice);
  }
  drawRequiredRoleList(document, document.json(), PaletteRoles,
                       runtime::terrainPaletteRoleName, notice);
}

} // namespace

bool drawEditorDataAssetControls(EditorWorkspace &workspace,
                                 EditorJsonDocument &document,
                                 const std::string_view contentType,
                                 std::string &notice) {
  if (contentType != "terrain_material" &&
      contentType != "terrain_material_set" && contentType != "terrain_palette")
    return false;

  const std::string documentId = document.path().generic_string();
  ImGui::PushID(documentId.c_str());
  ImGui::TextWrapped(
      "Terrain PBR shading is pending. This editor authors "
      "materials and palettes; it does not preview surface shading.");
  if (contentType == "terrain_material") {
    drawMaterial(workspace, document, notice);
  } else if (contentType == "terrain_material_set") {
    drawMaterialSet(workspace, document, notice);
  } else {
    drawPalette(workspace, document, notice);
  }
  ImGui::PopID();
  return true;
}

} // namespace demi::editor
