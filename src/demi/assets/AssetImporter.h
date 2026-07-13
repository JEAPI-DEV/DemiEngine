#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <optional>
#include <string>

namespace demi::assets {

struct ImporterDescriptor {
  std::string name;
  int version = 1;
  std::string assetType;
};

struct AssetImportRequest {
  std::filesystem::path projectDirectory;
  std::filesystem::path source;
  std::string id;
  std::string type;
  std::optional<std::filesystem::path> license;
};

struct AssetImportResult {
  std::filesystem::path manifestPath;
  Diagnostics diagnostics;
};

[[nodiscard]] std::optional<ImporterDescriptor>
importerFor(const std::filesystem::path &source, const std::string &type = {});
[[nodiscard]] AssetImportResult importAsset(const AssetImportRequest &request);
[[nodiscard]] Diagnostics
reimportAsset(const std::filesystem::path &manifestPath);

} // namespace demi::assets
