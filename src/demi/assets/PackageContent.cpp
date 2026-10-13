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
#include <optional>
#include <set>
#include <span>
#include <stdexcept>

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
    entries.push_back({{"path", file.lexically_relative(root).generic_string()},
                       {"hash", *hash}});
  }
  const std::string canonical = entries.dump();
  return hashBytes(
      std::span(reinterpret_cast<const unsigned char *>(canonical.data()),
                canonical.size()));
}

struct CookedPackageMetadata {
  std::map<std::string, std::string> fileHashes;
  std::map<std::string, std::pair<std::string, std::string>> assetOrigins;
};

std::optional<CookedPackageMetadata> loadCookedPackageMetadata(
    const std::filesystem::path &projectDirectory, Diagnostics &diagnostics) {
  const auto path = projectDirectory / "cook.manifest.json";
  try {
    std::ifstream input(path);
    if (!input)
      throw std::runtime_error("Cook manifest cannot be read.");
    const auto document = nlohmann::json::parse(input);
    if (!document.contains("files") || !document.at("files").is_array() ||
        !document.contains("assets") || !document.at("assets").is_array())
      throw std::runtime_error("Cook manifest omits files or asset provenance.");
    CookedPackageMetadata metadata;
    for (const auto &entry : document.at("files")) {
      const std::string relative = entry.at("path").get<std::string>();
      const std::string hash = entry.at("hash").get<std::string>();
      if (!packages::safePackageRelativePath(relative) || hash.empty() ||
          !metadata.fileHashes.emplace(relative, hash).second)
        throw std::runtime_error("Cook manifest has an invalid file entry.");
    }
    for (const auto &entry : document.at("assets")) {
      const std::string id = entry.at("asset").get<std::string>();
      const std::string package = entry.value("source_package", "");
      const std::string hash = entry.value("package_content_hash", "");
      if (id.empty() || !metadata.assetOrigins.emplace(id, std::pair{package, hash}).second)
        throw std::runtime_error("Cook manifest has an invalid asset entry.");
    }
    return metadata;
  } catch (const std::exception &exception) {
    error(diagnostics, "COOK_MANIFEST_INVALID", exception.what(), path);
    return std::nullopt;
  }
}

bool matchesCookManifest(const CookedPackageMetadata &metadata,
                         const std::filesystem::path &projectDirectory,
                         const std::filesystem::path &file,
                         Diagnostics &diagnostics) {
  if (!pathIsInside(projectDirectory, file)) {
    error(diagnostics, "PACKAGE_COOKED_FILE_UNSAFE",
          "Cooked package file escapes the project.", file);
    return false;
  }
  const auto relative =
      std::filesystem::relative(file, projectDirectory).generic_string();
  const auto expected = metadata.fileHashes.find(relative);
  const auto actual = hashFile(file);
  if (expected == metadata.fileHashes.end() || !actual ||
      *actual != expected->second) {
    error(diagnostics, "PACKAGE_COOKED_FILE_MISMATCH",
          "Cooked package file is missing or differs from the cook manifest.",
          file);
    return false;
  }
  return true;
}

std::map<std::string, std::string> cookedTerrainSubstitutions(
    const std::filesystem::path &projectDirectory,
    const std::filesystem::path &packageRoot,
    const std::string &packageName,
    const packages::PackageRelease &release,
    const CookedPackageMetadata &metadata, Diagnostics &diagnostics) {
  std::map<std::string, std::string> substitutions;
  std::string sourceContentHash;
  for (const std::string &relativeManifest : release.manifest.assetManifests) {
    const auto manifestPath = packageRoot / relativeManifest;
    Diagnostic diagnostic;
    const auto asset = loadAssetManifest(manifestPath, &diagnostic);
    if (!asset) {
      diagnostics.push_back(std::move(diagnostic));
      continue;
    }
    if (asset->type != "Terrain")
      continue;
    if (asset->importer != "terrain_heightfield" ||
        !asset->sourcePath.filename().string().ends_with(".terrain.bin") ||
        !pathIsInside(packageRoot, asset->sourcePath)) {
      error(diagnostics, "PACKAGE_COOKED_TERRAIN_INVALID",
            "Cooked Terrain asset must reference a binary in its package.",
            manifestPath);
      continue;
    }
    const auto binary =
        std::filesystem::relative(asset->sourcePath, packageRoot);
    auto authored = binary;
    authored.replace_extension(".json");
    const std::string authoredPath = authored.generic_string();
    const std::string binaryPath = binary.generic_string();
    const auto origin = metadata.assetOrigins.find(asset->id);
    if (!packages::safePackageRelativePath(binaryPath) ||
        std::ranges::find(release.manifest.files, authoredPath) ==
            release.manifest.files.end() ||
        std::ranges::find(release.manifest.files, binaryPath) !=
            release.manifest.files.end() ||
        origin == metadata.assetOrigins.end() ||
        origin->second.first != packageName ||
        origin->second.second.empty() ||
        (!sourceContentHash.empty() &&
         sourceContentHash != origin->second.second) ||
        !matchesCookManifest(metadata, projectDirectory,
                             asset->sourcePath, diagnostics) ||
        !matchesCookManifest(metadata, projectDirectory,
                             manifestPath, diagnostics) ||
        hashFiles(asset->sourcePaths) !=
            std::optional<std::string>(asset->sourceHash)) {
      error(diagnostics, "PACKAGE_COOKED_TERRAIN_INVALID",
            "Cooked Terrain does not match its locked source and cook provenance.",
            manifestPath);
      continue;
    }
    sourceContentHash = origin->second.second;
    if (!substitutions.emplace(authoredPath, binaryPath).second)
      error(diagnostics, "PACKAGE_COOKED_TERRAIN_DUPLICATE",
            "Two Terrain assets replace the same locked source.", manifestPath);
  }
  return substitutions;
}

} // namespace

static LockedPackageContent
loadLockedPackageContentImpl(const std::filesystem::path &projectDirectory,
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

  // Cooked games retain the lock as the public-content manifest, but omit
  // installer state and author-only files (README, tests, docs).
  const bool cooked =
      std::filesystem::is_regular_file(projectDirectory / "cook.manifest.json");
  const auto cookedMetadata = cooked
      ? loadCookedPackageMetadata(projectDirectory, result.diagnostics)
      : std::optional<CookedPackageMetadata>{};
  if (cooked && !cookedMetadata)
    return result;

  std::map<std::string, std::string> owners;
  if (projectAssets != nullptr)
    for (const AssetManifest &asset : projectAssets->assets)
      owners.emplace(asset.id, "project");

  for (const auto &[packageName, release] : lock.releases) {
    const auto root = projectDirectory /
                      (cooked ? "packages" : ".demi/packages") / packageName;
    if (!cooked) {
      auto installed = packages::loadPackageManifest(
          root / packages::PackageManifestFilename);
      if (!installed.manifest)
        for (auto &diagnostic : installed.diagnostics)
          if (diagnostic.suggestion.empty() &&
              diagnostic.code == "PACKAGE_MANIFEST_READ_FAILED")
            diagnostic.suggestion =
                "Check this project's dependency installation. Restore missing "
                "packages with demi package install --locked from the project "
                "directory.";
      result.diagnostics.insert(result.diagnostics.end(),
                                installed.diagnostics.begin(),
                                installed.diagnostics.end());
      if (!installed.manifest)
        continue;
      if (packages::packageManifestJson(*installed.manifest).dump() !=
          packages::packageManifestJson(release.manifest).dump()) {
        error(result.diagnostics, "PACKAGE_CONTENT_LOCK_MISMATCH",
              "Installed package content does not match the verified lock.",
              root);
        continue;
      }
    }
    const auto substitutions = cooked
        ? cookedTerrainSubstitutions(projectDirectory, root, packageName,
                                     release, *cookedMetadata,
                                     result.diagnostics)
        : std::map<std::string, std::string>{};
    std::vector<std::filesystem::path> packageFiles;
    for (const std::string &relative : release.manifest.files) {
      if (cooked && !isRuntimePackageFile(relative))
        continue;
      const auto replacement = substitutions.find(relative);
      const auto file = root /
          (replacement == substitutions.end() ? relative : replacement->second);
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
    for (const std::string &relative : release.manifest.assetManifests) {
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
    for (const std::string &relative : release.manifest.engineExtensions) {
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
            std::ranges::find(release.manifest.files, entryPath) !=
            release.manifest.files.end();
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

LockedPackageContent
loadLockedPackageContent(const std::filesystem::path &projectDirectory,
                         const std::string_view platform,
                         const AssetRegistry *projectAssets) {
  try {
    return loadLockedPackageContentImpl(projectDirectory, platform,
                                        projectAssets);
  } catch (const std::filesystem::filesystem_error &exception) {
    LockedPackageContent result;
    error(result.diagnostics, "PACKAGE_CONTENT_UNAVAILABLE",
          "Package content changed or became inaccessible while being read: " +
              exception.code().message(),
          exception.path1().empty() ? projectDirectory : exception.path1());
    return result;
  }
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
