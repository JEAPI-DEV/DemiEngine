#include "demi/packages/PackageLock.h"

#include "demi/packages/PackageHash.h"

#include <fstream>

namespace demi::packages {
namespace {

void addError(Diagnostics &diagnostics, std::string code, std::string message,
              const std::filesystem::path &path) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string()});
}

} // namespace

PackageLockLoadResult loadPackageLock(const std::filesystem::path &path) {
  PackageLockLoadResult result;
  std::ifstream input(path);
  if (!input) {
    addError(result.diagnostics, "PACKAGE_LOCK_READ_FAILED",
             "Could not read the package lock file.", path);
    return result;
  }
  nlohmann::json document;
  try {
    input >> document;
  } catch (const nlohmann::json::exception &exception) {
    addError(result.diagnostics, "PACKAGE_LOCK_INVALID_JSON", exception.what(),
             path);
    return result;
  }
  if (document.value("format_version", 0) != 1 ||
      !document.contains("packages") || !document["packages"].is_array()) {
    addError(result.diagnostics, "PACKAGE_LOCK_FORMAT_UNSUPPORTED",
             "Package lock requires format_version 1 and a packages array.",
             path);
    return result;
  }
  result.registry = document.value("registry", "");
  for (const auto &entry : document["packages"]) {
    if (!entry.is_object() || !entry.contains("manifest")) {
      addError(result.diagnostics, "PACKAGE_LOCK_ENTRY_INVALID",
               "Package lock contains an invalid entry.", path);
      continue;
    }
    auto loaded = parsePackageManifest(entry["manifest"], path.string());
    result.diagnostics.insert(result.diagnostics.end(),
                              loaded.diagnostics.begin(),
                              loaded.diagnostics.end());
    const std::string hash = entry.value("archive_hash", "");
    const std::string manifestHash = entry.value("manifest_hash", "");
    const std::string uri = entry.value("archive_uri", "");
    const std::string actualManifestHash =
        loaded.manifest
            ? sha256Text(packageManifestJson(*loaded.manifest).dump())
            : std::string{};
    if (!loaded.manifest || !hash.starts_with("sha256:") || uri.empty() ||
        manifestHash != actualManifestHash) {
      addError(result.diagnostics, "PACKAGE_LOCK_ENTRY_INVALID",
               "Locked package omits a valid manifest, archive hash, or URI.",
               path);
      continue;
    }
    if (!result.releases
             .emplace(loaded.manifest->name,
                      PackageRelease{.manifest = *loaded.manifest,
                                     .manifestHash = manifestHash,
                                     .archiveHash = hash,
                                     .archiveUri = uri,
                                     .yanked = entry.value("yanked", false)})
             .second)
      addError(result.diagnostics, "PACKAGE_LOCK_DUPLICATE",
               "Package lock contains a duplicate package: " +
                   loaded.manifest->name,
               path);
  }
  if (hasErrors(result.diagnostics))
    result.releases.clear();
  return result;
}

nlohmann::json
packageLockJson(const std::map<std::string, PackageRelease> &releases,
                const std::string &registry) {
  nlohmann::json packages = nlohmann::json::array();
  for (const auto &[unused, release] : releases) {
    (void)unused;
    packages.push_back(
        {{"manifest", packageManifestJson(release.manifest)},
         {"manifest_hash",
          release.manifestHash.empty()
              ? sha256Text(packageManifestJson(release.manifest).dump())
              : release.manifestHash},
         {"archive_hash", release.archiveHash},
         {"archive_uri", release.archiveUri},
         {"yanked", release.yanked}});
  }
  return {{"format_version", 1},
          {"registry", registry},
          {"packages", std::move(packages)}};
}

} // namespace demi::packages
