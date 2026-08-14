#pragma once

#include "demi/assets/AssetRegistry.h"

#include <filesystem>
#include <vector>

namespace demi::assets {

struct GeneratedAtlasCookResult {
  std::vector<std::filesystem::path> outputs;
  Diagnostics diagnostics;
};

[[nodiscard]] Diagnostics
validateGeneratedAtlasManifest(const AssetManifest &asset,
                               const AssetRegistry &registry);

// Generates deterministic texture/font atlas pages and metadata for the two
// generated atlas asset types. Other asset types return an empty result.
[[nodiscard]] GeneratedAtlasCookResult
cookGeneratedAtlas(const AssetManifest &asset, const AssetRegistry &registry,
                   const std::filesystem::path &outputDirectory);

} // namespace demi::assets
