#include "demi/assets/MaterialSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace demi::assets {
namespace {
struct SlotName {
  MaterialMapSlot slot;
  std::string_view name;
};
constexpr std::array slots{
    SlotName{MaterialMapSlot::Albedo, "albedo"},
    SlotName{MaterialMapSlot::Normal, "normal"},
    SlotName{MaterialMapSlot::Roughness, "roughness"},
    SlotName{MaterialMapSlot::Metallic, "metallic"},
    SlotName{MaterialMapSlot::AmbientOcclusion, "ambient_occlusion"},
    SlotName{MaterialMapSlot::Height, "height"},
    SlotName{MaterialMapSlot::DetailNormal, "detail_normal"},
    SlotName{MaterialMapSlot::Emissive, "emissive"},
};
} // namespace

std::string_view materialMapSlotName(MaterialMapSlot slot) {
  for (const auto &entry : slots) {
    if (entry.slot == slot) {
      return entry.name;
    }
  }
  return {};
}

std::optional<MaterialMapSlot> materialMapSlotFromName(std::string_view name) {
  for (const auto &entry : slots) {
    if (entry.name == name) {
      return entry.slot;
    }
  }
  return std::nullopt;
}

MaterialSurface materialSurface(const MaterialAsset &material) {
  MaterialSurface surface;
  const auto number = [&](const char *name, float fallback) {
    const auto value = material.numbers.find(name);
    return value == material.numbers.end() ? fallback : value->second;
  };
  if (const auto color = material.colors.find("base_color");
      color != material.colors.end()) {
    surface.baseColor = color->second;
  }
  surface.roughness = number("roughness", surface.roughness);
  surface.metallic = number("metallic", surface.metallic);
  surface.normalStrength = number("normal_strength", surface.normalStrength);
  surface.tiling = number("tiling", surface.tiling);
  surface.detailStrength = number("detail_strength", surface.detailStrength);
  surface.foliageTransmission = number("foliage_transmission", surface.foliageTransmission);
  surface.ambientOcclusionStrength = number("ambient_occlusion_strength", surface.ambientOcclusionStrength);
  surface.windStrength = number("wind_strength", surface.windStrength);
  surface.windFrequency = number("wind_frequency", surface.windFrequency);
  surface.windHeight = number("wind_height", surface.windHeight);
  surface.heightBlendStrength =
      number("height_blend_strength", surface.heightBlendStrength);
  surface.macroVariationStrength =
      number("macro_variation_strength", surface.macroVariationStrength);
  surface.macroVariationScale =
      number("macro_variation_scale", surface.macroVariationScale);
  surface.mapping = material.mapping == "triplanar" ? MaterialMapping::Triplanar
                                                    : MaterialMapping::Uv;
  surface.normalMapYDown = material.normalMapConvention == "directx";
  for (const auto &[name, reference] : material.textures) {
    if (const auto slot = materialMapSlotFromName(name)) {
      surface.maps.emplace(*slot, reference);
    }
  }
  return surface;
}

Diagnostics validateMaterialSurface(const MaterialAsset &material,
                                    const std::filesystem::path &source) {
  Diagnostics diagnostics;
  const auto report = [&](std::string code, const std::string &field,
                          std::string message) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = std::move(code),
                           .message = std::move(message),
                           .path = source.string() + "#/" + field});
  };
  if (material.mapping != "uv" && material.mapping != "triplanar") {
    report("MATERIAL_SURFACE_POLICY_INVALID", "mapping",
           "Material mapping must be uv or triplanar.");
  }
  if (material.normalMapConvention != "opengl" &&
      material.normalMapConvention != "directx") {
    report("MATERIAL_SURFACE_POLICY_INVALID", "normal_map_convention",
           "Material normal_map_convention must be opengl or directx.");
  }
  for (const auto &[name, number] : material.numbers) {
    if (!std::isfinite(number)) {
      report("MATERIAL_PARAMETER_NOT_FINITE", "parameters/" + name,
             "Material parameters must fit finite floats: " + name);
    }
  }
  for (const auto &[name, color] : material.colors) {
    const std::array channels{color.r, color.g, color.b, color.a};
    const bool finite = std::ranges::all_of(
        channels, [](float channel) { return std::isfinite(channel); });
    if (!finite) {
      report("MATERIAL_PARAMETER_NOT_FINITE", "parameters/" + name,
             "Material colors must fit finite floats: " + name);
    }
  }
  if (hasErrors(diagnostics)) {
    return diagnostics;
  }
  if (!material.shader.starts_with("builtin://")) {
    return diagnostics;
  }
  for (const auto &[slot, reference] : material.textures) {
    if (!materialMapSlotFromName(slot)) {
      report("MATERIAL_TEXTURE_SLOT_INVALID", "textures/" + slot,
             "Unknown built-in material texture slot: " + slot);
    }
  }
  for (const auto &[name, number] : material.numbers) {
    const bool normalized =
        name == "roughness" || name == "metallic" || name == "opacity" ||
        name == "height_blend_strength" || name == "macro_variation_strength" ||
        name == "foliage_transmission" || name == "ambient_occlusion_strength";
    const bool nonnegative =
        name == "normal_strength" || name == "detail_strength" ||
        name == "wind_strength" || name == "wind_frequency";
    const bool positive = name == "tiling" || name == "macro_variation_scale" ||
                          name == "wind_height";
    const bool wrongType = name == "base_color";
    const bool outOfRange = (normalized && (number < 0 || number > 1)) ||
                            (nonnegative && number < 0) ||
                            (positive && number <= 0);
    if (!std::isfinite(number) || wrongType || outOfRange) {
      report("MATERIAL_SURFACE_PARAMETER_INVALID", "parameters/" + name,
             "Invalid built-in material parameter: " + name);
    }
  }
  for (const auto &[name, color] : material.colors) {
    const bool numericOnly =
        name == "roughness" || name == "metallic" || name == "opacity" ||
        name == "normal_strength" || name == "tiling" ||
        name == "detail_strength" || name == "height_blend_strength" ||
        name == "macro_variation_strength" || name == "macro_variation_scale" ||
        name == "wind_strength" || name == "wind_frequency" ||
        name == "wind_height" || name == "foliage_transmission" ||
        name == "ambient_occlusion_strength";
    const bool normalized = name == "base_color";
    const std::array channels{color.r, color.g, color.b, color.a};
    const bool validChannels =
        std::ranges::all_of(channels, [&](float channel) {
          return std::isfinite(channel) &&
                 (!normalized || (channel >= 0 && channel <= 1));
        });
    if (numericOnly || !validChannels) {
      report("MATERIAL_SURFACE_PARAMETER_INVALID", "parameters/" + name,
             "Invalid built-in material parameter: " + name);
    }
  }
  return diagnostics;
}

} // namespace demi::assets
