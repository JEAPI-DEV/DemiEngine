#include "demi/assets/DataAssetContent.h"

#include "demi/assets/MaterialSet.h"
#include "demi/assets/TerrainRegionAsset.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <exception>
#include <stdexcept>

namespace demi::assets {

int dataAssetContentFormatVersion(std::string_view contentType) {
  return contentType == "terrain_palette"
             ? runtime::TerrainPalette::CurrentFormatVersion
             : 1;
}

DataAssetContentResult inspectDataAssetContent(
    const std::string_view contentType, const DataDocument &document,
    const AssetRegistry &registry, const std::string_view assetId) {
  DataAssetContentResult result;
  try {
    if (contentType == "terrain_material") {
      throw std::invalid_argument(
          "terrain_material DataAssets were removed. Migrate this definition "
          "to an ordinary Material asset.");
    } else if (contentType == "terrain_material_set") {
      const auto set = parseTerrainMaterialSet(document, registry, assetId);
      if (set)
        result.dependencies = set->assetDependencies();
      else
        throw std::invalid_argument(
            "terrain material set content could not be parsed.");
    } else if (contentType == "terrain_region") {
      const auto region = parseTerrainRegionAsset(document);
      const auto *terrain = findAsset(registry, region.terrain);
      if (!terrain || terrain->type != "Terrain") {
        throw std::invalid_argument("Terrain region must reference a Terrain asset.");
      }
      if (terrain->sourcePath.filename().string().ends_with(".terrain.json")) {
        (void)compileTerrainRegionSource(registry, region);
      }
      result.dependencies.push_back(region.terrain);
    } else if (contentType == "terrain_palette") {
      result.dependencies =
          runtime::parseTerrainPalette(document, registry, assetId)
              .assetDependencies();
    }
    // Manifest dependency traversal resolves only asset:// IDs. The native
    // palette still retains prefab:// references for source-level consumers.
    std::erase_if(result.dependencies, [](const std::string &id) {
      return !id.starts_with("asset://");
    });
  } catch (const std::exception &failure) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "DATA_CONTENT_INVALID",
                                  .message = failure.what(),
                                  .path = std::string(assetId)});
  }
  return result;
}

} // namespace demi::assets
