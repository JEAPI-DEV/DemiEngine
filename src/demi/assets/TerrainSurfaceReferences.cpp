#include "demi/assets/TerrainSurfaceReferences.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/runtime/terrain/TerrainRecipe.h"

namespace demi::assets {
Diagnostics
validateTerrainSurfaceReferences(const AssetRegistry &registry,
                                 const runtime::TerrainRecipe &recipe,
                                 const std::filesystem::path &source) {
  Diagnostics diagnostics;
  for (const auto &[id, biome] : recipe.biomes) {
    if (biome.material.empty())
      continue;
    const auto *material = findAsset(registry, biome.material);
    const auto path = source.string() + "#/biomes/" + id + "/material";
    if (!material) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "TERRAIN_SURFACE_MATERIAL_MISSING",
           .message = "Terrain biome material was not found: " + biome.material,
           .path = path});
      continue;
    }
    if (material->type == "Material")
      continue;
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "TERRAIN_SURFACE_MATERIAL_TYPE_INVALID",
         .message = "Terrain biome surfaces require an ordinary Material "
                    "asset. Migrate terrain_material DataAssets to Material.",
         .path = path});
  }
  return diagnostics;
}
} // namespace demi::assets
