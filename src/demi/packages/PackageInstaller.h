#pragma once

#include "demi/packages/PackageRegistry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace demi::packages {

inline constexpr std::string_view PackageLockFilename =
    "demi.packages.lock.json";

struct PackageInstallOptions {
  std::filesystem::path projectDirectory;
  std::filesystem::path cacheDirectory;
  bool offline = false;
  bool dryRun = false;
  std::optional<nlohmann::json> replacementProject;
  std::function<Diagnostics(const std::filesystem::path &)> validateStaging;
};

struct PackageLockLoadResult {
  std::map<std::string, PackageRelease> releases;
  std::string registry;
  Diagnostics diagnostics;
};

[[nodiscard]] PackageLockLoadResult
loadPackageLock(const std::filesystem::path &path);
[[nodiscard]] nlohmann::json
packageLockJson(const std::map<std::string, PackageRelease> &releases,
                const std::string &registry);
[[nodiscard]] Diagnostics
installPackages(PackageRegistry &registry,
                const std::map<std::string, PackageRelease> &releases,
                const PackageInstallOptions &options);

} // namespace demi::packages
