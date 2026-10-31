#pragma once

#include "demi/runtime/terrain/RegionalTerrainSampler.h"

#include <set>
#include <string>
#include <string_view>

namespace demi {
struct AssetRegistry;
struct AssetManifest;
} // namespace demi
namespace demi::assets {
class DataDocument;
[[nodiscard]] bool isTerrainRegionAsset(const AssetManifest &manifest);
// Ordinary Terrain assets consumed as generation programs by region wrappers.
[[nodiscard]] std::set<std::string>
terrainRegionSourceIds(const AssetRegistry &registry);

// Regional placement/sampling references an ordinary Terrain asset. Its recipe,
// graph and prefab palette remain owned by the existing terrain authoring path.
struct TerrainRegionDefinition {
  std::string terrain;
  runtime::TerrainRegionStreamingSettings streaming;
  double originX = 0;
  double originZ = 0;
  double baseSampleSpacing = 0;
  double normalSampleSpacing = 0;
};
[[nodiscard]] TerrainRegionDefinition
parseTerrainRegionAsset(const DataDocument &document);
[[nodiscard]] runtime::TerrainRegionConfig
loadTerrainRegionSource(const AssetRegistry &registry, std::string_view id);
[[nodiscard]] runtime::TerrainRegionConfig
compileTerrainRegionSource(const AssetRegistry &registry,
                           const TerrainRegionDefinition &definition);

} // namespace demi::assets
