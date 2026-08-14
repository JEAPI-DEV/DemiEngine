#pragma once

#include "demi/packages/PackageRegistry.h"
#include "demi/packages/PackageLock.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace demi::packages {

struct PackageInstallOptions {
  std::filesystem::path projectDirectory;
  std::filesystem::path cacheDirectory;
  bool offline = false;
  bool dryRun = false;
  std::optional<nlohmann::json> replacementProject;
  std::function<Diagnostics(const std::filesystem::path &)> validateStaging;
};

[[nodiscard]] Diagnostics
installPackages(PackageRegistry &registry,
                const std::map<std::string, PackageRelease> &releases,
                const PackageInstallOptions &options);

} // namespace demi::packages
