#pragma once

#include "demi/packages/PackageResolver.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace demi::packages {

inline constexpr std::string_view PackageLockFilename =
    "demi.packages.lock.json";

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

} // namespace demi::packages
