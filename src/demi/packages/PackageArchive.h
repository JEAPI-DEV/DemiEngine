#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/packages/PackageManifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace demi::packages {

struct PackageArchiveInfo {
  PackageManifest manifest;
  std::string archiveHash;
};

[[nodiscard]] std::optional<std::string>
sha256File(const std::filesystem::path &path);
[[nodiscard]] std::string sha256Text(std::string_view text);
[[nodiscard]] Diagnostics
createPackageArchive(const std::filesystem::path &packageRoot,
                     const std::filesystem::path &outputPath);
[[nodiscard]] std::optional<PackageArchiveInfo>
inspectPackageArchive(const std::filesystem::path &archivePath,
                      Diagnostics &diagnostics);
[[nodiscard]] std::optional<PackageArchiveInfo>
extractPackageArchive(const std::filesystem::path &archivePath,
                      const std::filesystem::path &destination,
                      Diagnostics &diagnostics);

} // namespace demi::packages
