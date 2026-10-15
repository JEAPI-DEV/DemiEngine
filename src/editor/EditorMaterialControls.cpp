#include "editor/EditorMaterialControls.h"

#include "demi/assets/MaterialSurface.h"
#include "editor/EditorAssetReferenceControl.h"
#include "editor/EditorColorControl.h"
#include "editor/EditorJsonDocument.h"
#include "editor/EditorWorkspace.h"

#include <array>
#include <imgui.h>
#include <initializer_list>
#include <limits>
#include <utility>

namespace demi::editor {
namespace {
constexpr assets::MaterialMapSlot MapSlots[] = {
    assets::MaterialMapSlot::Albedo,
    assets::MaterialMapSlot::Normal,
    assets::MaterialMapSlot::Roughness,
    assets::MaterialMapSlot::Metallic,
    assets::MaterialMapSlot::AmbientOcclusion,
    assets::MaterialMapSlot::Height,
    assets::MaterialMapSlot::DetailNormal,
    assets::MaterialMapSlot::Emissive};

void commit(EditorJsonDocument &document, const std::string &pointer,
            nlohmann::json value, std::string &notice) {
  std::string error;
  notice = document.set(pointer, std::move(value), error) ? "Material updated"
                                                          : error;
}

void choice(EditorJsonDocument &document, const char *label, const char *key,
            std::string current, std::initializer_list<const char *> values,
            std::string &notice) {
  if (ImGui::BeginCombo(label, current.c_str())) {
    for (const auto value : values) {
      if (ImGui::Selectable(value, current == value)) {
        commit(document, "/" + std::string(key), value, notice);
      }
    }
    ImGui::EndCombo();
  }
}
} // namespace

bool setEditorMaterialMap(EditorJsonDocument &document,
                          assets::MaterialMapSlot slot,
                          const std::string &reference, std::string &error) {
  auto replacement = document.json();
  const std::string name(assets::materialMapSlotName(slot));
  if (reference.empty()) {
    if (replacement.contains("textures")) {
      replacement["textures"].erase(name);
    }
    const auto parameters =
        replacement.value("parameters", nlohmann::json::object());
    if (slot == assets::MaterialMapSlot::DetailNormal &&
        parameters.value("detail_strength", 0.0F) > 0) {
      replacement["parameters"]["detail_strength"] = 0;
    }
  } else {
    replacement["textures"][name] = reference;
  }
  return document.replace(std::move(replacement), error);
}

void drawEditorMaterialSurfaceControls(EditorWorkspace &workspace,
                                       EditorJsonDocument &document,
                                       std::string &notice) {
  const auto parsed =
      assets::parseMaterialAsset(document.json(), document.path());
  if (!parsed) {
    ImGui::TextDisabled(
        "Fix material diagnostics before editing surface controls.");
    return;
  }
  const auto surface = assets::materialSurface(*parsed);
  std::array<float, 4> color{surface.baseColor.r, surface.baseColor.g,
                             surface.baseColor.b, surface.baseColor.a};
  if (drawEditorColorControl("Base color (sRGB)", color.data())) {
    commit(document, "/parameters/base_color", color, notice);
  }
  const auto scalar = [&](const char *label, const char *key, float current,
                          float minimum, float maximum) {
    if (ImGui::DragFloat(label, &current, .01F, minimum, maximum, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp |
                             ImGuiSliderFlags_NoRoundToFormat)) {
      commit(document, "/parameters/" + std::string(key), current, notice);
    }
  };
  scalar("Roughness", "roughness", surface.roughness, 0, 1);
  scalar("Metallic", "metallic", surface.metallic, 0, 1);
  scalar("Normal strength", "normal_strength", surface.normalStrength, 0,
         std::numeric_limits<float>::max());
  scalar("Tiling", "tiling", surface.tiling,
         std::numeric_limits<float>::denorm_min(),
         std::numeric_limits<float>::max());
  choice(document, "Mapping", "mapping", parsed->mapping, {"uv", "triplanar"},
         notice);
  ImGui::TextWrapped("UV uses mesh coordinates. Triplanar projects onto three "
                     "surface axes to reduce stretching on steep faces.");
  choice(document, "Normal map convention", "normal_map_convention",
         parsed->normalMapConvention, {"opengl", "directx"}, notice);
  ImGui::TextWrapped(
      "OpenGL normals use +Y (green up); DirectX normals use -Y (green down). "
      "Choose the convention of the imported maps.");

  ImGui::SeparatorText("Terrain layer shading");
  scalar("Height blend strength", "height_blend_strength",
         surface.heightBlendStrength, 0, 1);
  scalar("Macro variation strength", "macro_variation_strength",
         surface.macroVariationStrength, 0, 1);
  scalar("Macro variation scale", "macro_variation_scale",
         surface.macroVariationScale, std::numeric_limits<float>::denorm_min(),
         std::numeric_limits<float>::max());
  ImGui::TextWrapped(
      "Terrain layer controls do not displace geometry or change collision. "
      "Macro variation scale is cycles per terrain-local unit.");

  ImGui::SeparatorText("Texture maps");
  for (const auto slot : MapSlots) {
    const std::string name(assets::materialMapSlotName(slot));
    const auto found = surface.maps.find(slot);
    std::string reference = found == surface.maps.end() ? "" : found->second;
    if (!drawEditorAssetReferenceControl(
            name.c_str(), workspace.assetIndex().registry(),
            [](const AssetManifest &asset) {
              return asset.type == "Texture2D" || asset.type == "RenderTarget";
            },
            reference, true)) {
      continue;
    }
    std::string error;
    notice = setEditorMaterialMap(document, slot, reference, error)
                 ? "Material maps updated"
                 : error;
    return;
  }
  ImGui::TextWrapped(
      "Choose None to clear a map. Albedo and emissive colors use sRGB; normal "
      "and scalar maps contain linear data. Height influences GPU terrain "
      "layer blending; ordinary meshes do not sample it. It never changes "
      "geometry or collision.");
  const bool hasDetail =
      surface.maps.contains(assets::MaterialMapSlot::DetailNormal);
  ImGui::BeginDisabled(!hasDetail);
  scalar("Detail strength", "detail_strength", surface.detailStrength, 0,
         std::numeric_limits<float>::max());
  ImGui::EndDisabled();
  if (!hasDetail) {
    ImGui::TextDisabled("Assign detail_normal to enable detail strength.");
  }
}
} // namespace demi::editor
