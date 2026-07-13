#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::assets {

struct AssetPackageExportRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path outputPath;
  std::vector<std::string> assetIds;
};

struct AssetPackageImportRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path packagePath;
  bool overwrite = false;
};

[[nodiscard]] Diagnostics
exportAssetPackage(const AssetPackageExportRequest &request);
[[nodiscard]] Diagnostics
importAssetPackage(const AssetPackageImportRequest &request,
                   std::vector<std::string> *conflicts = nullptr);

} // namespace demi::assets
