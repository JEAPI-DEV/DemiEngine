#pragma once

#include "demi/assets/AssetRegistry.h"

#include <filesystem>
#include <vector>

namespace demi::assets {

[[nodiscard]] std::vector<std::filesystem::path>
collectAssetFiles(const AssetManifest &manifest);
[[nodiscard]] std::vector<std::filesystem::path>
collectReferencedSourceFiles(const std::filesystem::path &source);
[[nodiscard]] bool pathIsInside(const std::filesystem::path &root,
                                const std::filesystem::path &path);

} // namespace demi::assets
