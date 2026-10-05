#pragma once

#include "demi/assets/AssetRegistry.h"

#include <nlohmann/json_fwd.hpp>

namespace demi::assets {

// Derive the terrain-specific cook inputs from the same parsed source and
// referenced assets used by editor preparation.
[[nodiscard]] nlohmann::json
terrainAssetCookSettings(const AssetManifest &manifest,
                         const AssetRegistry &registry);

struct TerrainAssetCookResult {
  std::vector<std::filesystem::path> outputs;
  Diagnostics diagnostics;
};

// Publishes the cook manifest after removing only terrain recipes tracked by
// the preceding cook manifest. Untracked files are reported and left alone.
[[nodiscard]] Diagnostics publishCookManifestWithTerrainPrune(
    const std::filesystem::path &outputDirectory,
    const nlohmann::json &manifest);

// Replaces an authored terrain source with a cooked binary and rewrites its
// manifest within the cooked project. Both outputs participate in
// AssetCookCache.
[[nodiscard]] TerrainAssetCookResult
cookRegisteredTerrainAsset(const AssetManifest &manifest,
                           const AssetRegistry &registry,
                           const std::filesystem::path &outputDirectory,
                           const std::filesystem::path &relativeManifest,
                           const std::filesystem::path &relativeSource);

} // namespace demi::assets
