#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/packages/SemanticVersion.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace demi::packages {

inline constexpr std::string_view PackageManifestFilename =
    "demi.package.json";

struct PackageDependency {
  std::string name;
  VersionConstraint constraint;
};

struct PackageManifest {
  int formatVersion = 1;
  std::string name;
  SemanticVersion version;
  VersionConstraint engineVersion;
  std::vector<PackageDependency> dependencies;
  std::vector<std::string> publicModules;
  std::vector<std::string> files;
  std::vector<std::string> exportedEvents;
  std::filesystem::path sourcePath;
};

struct ManifestLoadResult {
  std::optional<PackageManifest> manifest;
  Diagnostics diagnostics;
};

[[nodiscard]] bool validPackageName(std::string_view name);
[[nodiscard]] bool safePackageRelativePath(std::string_view path);
[[nodiscard]] ManifestLoadResult
loadPackageManifest(const std::filesystem::path &path);
[[nodiscard]] ManifestLoadResult
parsePackageManifest(const nlohmann::json &document, std::string source);
[[nodiscard]] nlohmann::json packageManifestJson(const PackageManifest &manifest);

} // namespace demi::packages
