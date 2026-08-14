#pragma once

#include "demi/assets/AssetImporterRegistry.h"
#include "demi/assets/AssetRegistry.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi::assets {

struct PackageExtensionRegistration {
  std::string package;
  std::string id;
  std::string kind;
  std::filesystem::path entry;
  std::vector<std::string> platforms;
  std::optional<ImporterDescriptor> importer;
};

struct LockedPackageContent {
  std::vector<AssetManifest> assets;
  std::vector<PackageExtensionRegistration> extensions;
  std::vector<std::filesystem::path> files;
  Diagnostics diagnostics;
};

// Reads only the lock and installed tree created by the package installer.
// It deliberately has no registry dependency and performs no version solving.
[[nodiscard]] LockedPackageContent
loadLockedPackageContent(const std::filesystem::path &projectDirectory,
                         std::string_view platform,
                         const AssetRegistry *projectAssets = nullptr);
[[nodiscard]] Diagnostics
validatePackageRemoval(const std::filesystem::path &projectDirectory,
                       std::string_view packageName);

} // namespace demi::assets
