#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi {

struct AssetManifest {
  int formatVersion = 1;
  std::string id;
  std::string type;
  std::string importer;
  int importerVersion = 0;
  std::string sourceHash;
  std::vector<std::string> dependencies;
  std::string settingsJson;
  std::string attribution;
  std::filesystem::path manifestPath;
  std::filesystem::path sourcePath;
  std::vector<std::filesystem::path> sourcePaths;
  std::optional<std::filesystem::path> texturePath;
  std::optional<std::filesystem::path> atlasPath;
  std::optional<std::filesystem::path> generatedOutputPath;
  std::optional<std::filesystem::path> licensePath;
};

struct AssetRegistry {
  std::filesystem::path projectDirectory;
  std::vector<AssetManifest> assets;
  Diagnostics diagnostics;
};

[[nodiscard]] std::optional<AssetManifest>
loadAssetManifest(const std::filesystem::path &manifestPath,
                  Diagnostic *diagnostic = nullptr);
[[nodiscard]] AssetRegistry
loadAssetRegistry(const std::filesystem::path &projectDirectory);
[[nodiscard]] const AssetManifest *findAsset(const AssetRegistry &registry,
                                             const std::string &id);
[[nodiscard]] std::vector<const AssetManifest *>
assetDependencies(const AssetRegistry &registry, const AssetManifest &asset,
                  Diagnostics *diagnostics = nullptr);
[[nodiscard]] Diagnostics validateAssetRegistry(const AssetRegistry &registry);
[[nodiscard]] std::vector<std::string>
extractAssetReferences(const std::string &text);
[[nodiscard]] std::vector<std::string>
extractScriptReferences(const std::string &text);
[[nodiscard]] std::filesystem::path
resolveScriptReference(const std::filesystem::path &projectDirectory,
                       const std::string &reference);

} // namespace demi
