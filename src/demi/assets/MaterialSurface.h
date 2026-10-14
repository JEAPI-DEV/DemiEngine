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
  float roughness = 0.8F;
  float metallic = 0;
  float normalStrength = 1;
  float tiling = 1;
  float detailStrength = 0;
  MaterialMapping mapping = MaterialMapping::Uv;
  bool normalMapYDown = false;
  std::map<MaterialMapSlot, std::string> maps;
};

[[nodiscard]] std::string_view materialMapSlotName(MaterialMapSlot slot);
[[nodiscard]] std::optional<MaterialMapSlot>
materialMapSlotFromName(std::string_view name);
[[nodiscard]] MaterialSurface materialSurface(const MaterialAsset &material);
[[nodiscard]] Diagnostics
validateMaterialSurface(const MaterialAsset &material,
                        const std::filesystem::path &source);

} // namespace demi::assets
