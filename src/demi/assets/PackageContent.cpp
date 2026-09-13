#include "demi/assets/PackageContent.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/packages/PackageLock.h"
#include "demi/packages/PackageManifest.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>

namespace demi::assets {
namespace {

void error(Diagnostics &diagnostics, std::string code, std::string message,
           const std::filesystem::path &path) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = {}});
}

bool isRuntimePackageFile(const std::string &relative) {
  const std::filesystem::path path(relative);
  if (path.filename() == "README.md")
    return false;
  return std::ranges::none_of(path, [](const auto &component) {
    return component == "tests" || component == "examples" ||
           component == "docs";
  });
}

bool supportsPlatform(const std::vector<std::string> &platforms,
                      std::string_view platform) {
  return platform.empty() || platforms.empty() ||
         std::ranges::find(platforms, platform) != platforms.end();
}

std::optional<std::string>
packageContentHash(const std::filesystem::path &root,
                   const std::vector<std::filesystem::path> &files) {
  nlohmann::json entries = nlohmann::json::array();
  for (const auto &file : files) {
    const auto hash = hashFile(file);
    if (!hash)
      return std::nullopt;
    entries.push_back(
        {{"path", std::filesystem::relative(file, root).generic_string()},
         {"hash", *hash}});
  }
  const std::string canonical = entries.dump();
  return hashBytes(
      std::span(reinterpret_cast<const unsigned char *>(canonical.data()),
                canonical.size()));
}

} // namespace

LockedPackageContent
loadLockedPackageContent(const std::filesystem::path &projectDirectory,
                         const std::string_view platform,
                         const AssetRegistry *projectAssets) {
  LockedPackageContent result;
  const auto lockPath = projectDirectory / packages::PackageLockFilename;
  if (!std::filesystem::exists(lockPath))
    return result;
  const auto lock = packages::loadPackageLock(lockPath);
  result.diagnostics = lock.diagnostics;
  if (hasErrors(result.diagnostics))
    return result;

  std::map<std::string, std::string> owners;
  if (projectAssets != nullptr)
    for (const AssetManifest &asset : projectAssets->assets)
      owners.emplace(asset.id, "project");

  for (const auto &[packageName, release] : lock.releases) {
    const auto root = projectDirectory / ".demi/packages" / packageName;
    const auto installed =
        packages::loadPackageManifest(root / packages::PackageManifestFilename);
    result.diagnostics.insert(result.diagnostics.end(),
                              installed.diagnostics.begin(),
                              installed.diagnostics.end());
    if (!installed.manifest ||
        packages::packageManifestJson(*installed.manifest).dump() !=
            packages::packageManifestJson(release.manifest).dump()) {
      error(result.diagnostics, "PACKAGE_CONTENT_LOCK_MISMATCH",
            "Installed package content does not match the verified lock.",
            root);
      continue;
    }
    std::vector<std::filesystem::path> packageFiles;
    for (const std::string &relative : installed.manifest->files) {
      const auto file = root / relative;
      if (!std::filesystem::is_regular_file(file)) {
        error(result.diagnostics, "PACKAGE_CONTENT_FILE_MISSING",
              "A locked package content file is missing.", file);
        continue;
      }
      packageFiles.push_back(file);
      if (isRuntimePackageFile(relative))
        result.files.push_back(file);
    }
    const auto contentHash = packageContentHash(root, packageFiles);
    if (!contentHash) {
      error(result.diagnostics, "PACKAGE_CONTENT_HASH_FAILED",
            "Could not hash installed package content.", root);
      continue;
    }
    for (const std::string &relative : installed.manifest->assetManifests) {
      Diagnostic diagnostic;
      auto asset = loadAssetManifest(root / relative, &diagnostic);
      if (!asset) {
        result.diagnostics.push_back(std::move(diagnostic));
        continue;
      }
      const auto [owner, inserted] = owners.emplace(asset->id, packageName);
      if (!inserted) {
        error(result.diagnostics, "PACKAGE_ASSET_ID_CONFLICT",
              "Stable asset ID " + asset->id + " is exported by both " +
                  owner->second + " and " + packageName + ".",
              root / relative);
        continue;
      }
      asset->sourcePackage = packageName;
      asset->packageContentHash = *contentHash;
      result.assets.push_back(std::move(*asset));
    }
    for (const std::string &relative : installed.manifest->engineExtensions) {
      const auto descriptorPath = root / relative;
      try {
        std::ifstream input(descriptorPath);
        nlohmann::json document;
        input >> document;
        const std::string entryPath = document.value("entry", "");
        PackageExtensionRegistration extension{
            .package = packageName,
            .id = document.value("id", ""),
            .kind = document.value("kind", ""),
            .entry = root / entryPath,
            .platforms =
                document.value("platforms", std::vector<std::string>{}),
            .importer = std::nullopt};
        const bool declaredEntry =
            std::ranges::find(installed.manifest->files, entryPath) !=
            installed.manifest->files.end();
        if (document.value("format_version", 0) != 1 || extension.id.empty() ||
            (extension.kind != "asset_importer" &&
             extension.kind != "asset_handler") ||
            !packages::safePackageRelativePath(entryPath) || !declaredEntry ||
            !std::filesystem::is_regular_file(extension.entry)) {
          error(result.diagnostics, "PACKAGE_EXTENSION_INVALID",
                "Engine extension descriptor requires id, kind, and a declared "
                "entry.",
                descriptorPath);
          continue;
        }
        if (!supportsPlatform(extension.platforms, platform)) {
          error(result.diagnostics, "PACKAGE_EXTENSION_PLATFORM_UNSUPPORTED",
                "Package extension " + extension.id +
                    " does not support target " + std::string(platform) + ".",
                descriptorPath);
          continue;
        }
        if (extension.kind == "asset_importer") {
          ImporterDescriptor importer{
              .name = extension.id,
              .assetType = {},
              .version = document.value("version", 0),
              .settingsSchemaVersion =
                  document.value("settings_schema_version", 0),
              .extensions = document.value("supported_extensions",
                                           std::vector<std::string>{}),
              .assetTypes =
                  document.value("asset_types", std::vector<std::string>{}),
              .outputTypes =
                  document.value("output_types", std::vector<std::string>{}),
              .platforms = extension.platforms,
              .settingsSchema =
                  document.value("settings_schema", nlohmann::json::object())
                      .dump(),
              .threadSafe = document.value("thread_safe", false)};
          if (importer.version < 1 || importer.settingsSchemaVersion < 1 ||
              importer.extensions.empty() || importer.assetTypes.empty() ||
              importer.outputTypes.empty()) {
            error(result.diagnostics, "PACKAGE_IMPORTER_DESCRIPTOR_INVALID",
                  "Package importer must declare positive versions, source "
                  "extensions, asset types, and output types.",
                  descriptorPath);
            continue;
          }
          extension.importer = std::move(importer);
        }
        result.extensions.push_back(std::move(extension));
      } catch (const nlohmann::json::exception &exception) {
        error(result.diagnostics, "PACKAGE_EXTENSION_INVALID", exception.what(),
              descriptorPath);
      }
    }
  }
  std::ranges::sort(result.assets, {}, &AssetManifest::id);
  return result;
}

Diagnostics
validatePackageRemoval(const std::filesystem::path &projectDirectory,
                       const std::string_view packageName) {
  Diagnostics diagnostics;
  const LockedPackageContent content =
      loadLockedPackageContent(projectDirectory, "");
  diagnostics = content.diagnostics;
  std::set<std::string> packageAssetIds;
  for (const AssetManifest &asset : content.assets)
    if (asset.sourcePackage == packageName)
      packageAssetIds.insert(asset.id);
  if (packageAssetIds.empty())
    return diagnostics;

  const auto installedRoot = projectDirectory / ".demi/packages";
  for (const auto &path : collectKnownSourceFiles(projectDirectory)) {
    if (pathIsInside(installedRoot, path))
      continue;
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    for (const std::string &reference : extractAssetReferences(text))
      if (packageAssetIds.contains(reference))
        error(diagnostics, "PACKAGE_REMOVAL_AUTHORED_REFERENCE",
              "Package " + std::string(packageName) +
                  " cannot be removed while authored content references " +
                  reference + ".",
              path);
  }

  const auto cookedRoot = projectDirectory / "generated/cooked";
  if (std::filesystem::is_directory(cookedRoot))
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(cookedRoot)) {
      if (!entry.is_regular_file() ||
          entry.path().filename() != "cook.manifest.json")
        continue;
      try {
        std::ifstream input(entry.path());
        nlohmann::json manifest;
        input >> manifest;
        const bool ownsOutput = std::ranges::any_of(
            manifest.value("assets", nlohmann::json::array()),
            [&](const nlohmann::json &asset) {
              return asset.value("source_package", "") == packageName;
            });
        if (ownsOutput)
          error(diagnostics, "PACKAGE_REMOVAL_COOKED_OUTPUT",
                "Package cannot be removed while cooked outputs retain its "
                "content provenance.",
                entry.path());
      } catch (const nlohmann::json::exception &) {
        // Cook validation owns malformed cook-manifest diagnostics.
      }
    }
  return diagnostics;
}

} // namespace demi::assets
