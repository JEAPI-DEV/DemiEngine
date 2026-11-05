#pragma once

#include "demi/assets/RenderAsset.h"
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace demi::assets {

enum class MaterialMapSlot {
  Albedo,
  Normal,
  Roughness,
  Metallic,
  AmbientOcclusion,
  Height,
  DetailNormal,
  Emissive
};
enum class MaterialMapping { Uv, Triplanar };

// Typed surface values derived from an ordinary Material descriptor. Asset
// identity stays with its registry entry; this is not another authored format.
struct MaterialSurface {
  runtime::Color baseColor{1, 1, 1,
                           1}; // sRGB, matching ordinary Material tint.
  static constexpr float DefaultRoughness = 0.8F;
  static constexpr float DefaultOpacity = 1.0F;
  float roughness = DefaultRoughness;
  float metallic = 0;
  float opacity = DefaultOpacity;
  float normalStrength = 1;
  float tiling = 1;
  float detailStrength = 0;
  // Thin, non-refractive leaf transmission; zero retains ordinary opaque
  // lighting.
  float foliageTransmission = 0;
  float ambientOcclusionStrength = 1;
  // Visual world-space bending only; authored geometry and collision stay
  // fixed.
  float windStrength = 0;
  float windFrequency = 1;
  float windHeight = 1;
  // Terrain layer shading only; these never displace geometry or collision.
  float heightBlendStrength = 0.5F;
  float macroVariationStrength = 0;
  float macroVariationScale = 0.02F;
  MaterialMapping mapping = MaterialMapping::Uv;
  bool normalMapYDown = false;
  std::map<MaterialMapSlot, std::string> maps;
};

// Per-draw numeric precedence: explicit field, properties, inherited material,
// then canonical defaults. Invalid final values fall back, rather than inherit.
[[nodiscard]] float resolveMaterialSurfaceNumber(
    std::string_view field,
    const std::unordered_map<std::string, float> *properties,
    std::optional<float> explicitValue = {},
    std::optional<float> inherited = {});
[[nodiscard]] std::string_view materialSurfaceMode(std::string_view blend);

[[nodiscard]] std::string_view materialMapSlotName(MaterialMapSlot slot);
[[nodiscard]] std::optional<MaterialMapSlot>
materialMapSlotFromName(std::string_view name);
[[nodiscard]] MaterialSurface materialSurface(const MaterialAsset &material);
[[nodiscard]] Diagnostics
validateMaterialSurface(const MaterialAsset &material,
                        const std::filesystem::path &source);

} // namespace demi::assets
