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
    const auto metadata = material->type == "DataAsset"
                              ? dataAssetMetadata(*material)
                              : std::nullopt;
    const bool terrainDefinition =
        metadata && (metadata->contentType == "terrain_material" ||
                     metadata->contentType == "terrain_material_set");
    diagnostics.push_back(
        {.severity = terrainDefinition ? Severity::Warning : Severity::Error,
         .code = terrainDefinition ? "TERRAIN_SURFACE_DEFINITION_NOT_RENDERED"
                                   : "TERRAIN_SURFACE_MATERIAL_TYPE_INVALID",
         .message =
             terrainDefinition
                 ? "Typed terrain material definitions remain authoring data. "
                   "Assign an ordinary Material asset to render this biome."
                 : "Terrain biome surface requires a Material asset, not " +
                       material->type,
         .path = path});
  }
  return diagnostics;
}
} // namespace demi::assets
