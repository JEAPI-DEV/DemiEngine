#include "demi/assets/DataAssetContent.h"

#include "demi/assets/MaterialAsset.h"
#include "demi/assets/MaterialSet.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <exception>
#include <stdexcept>

namespace demi::assets {

DataAssetContentResult inspectDataAssetContent(
    const std::string_view contentType, const DataDocument &document,
    const AssetRegistry &registry, const std::string_view assetId) {
  DataAssetContentResult result;
  try {
    if (contentType == "terrain_material") {
      const auto material =
          parseTerrainMaterialAsset(document, registry, assetId);
      if (material)
        result.dependencies = material->assetDependencies();
      else
        throw std::invalid_argument(
            "terrain material content could not be parsed.");
    } else if (contentType == "terrain_material_set") {
      const auto set = parseTerrainMaterialSet(document, registry, assetId);
      if (set)
        result.dependencies = set->assetDependencies();
      else
        throw std::invalid_argument(
            "terrain material set content could not be parsed.");
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
