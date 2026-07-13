#include "demi/assets/AssetRegistry.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetSourceFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace demi {
namespace {

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::vector<std::string>
extractReferencesWithPrefix(const std::string &text,
                            const std::string &prefix) {
  std::vector<std::string> references;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t found = text.find(prefix, cursor);
    if (found == std::string::npos)
      break;
    std::size_t end = found;
    while (end < text.size()) {
      const char value = text[end];
      if (value == '"' || value == '\'' || value == ',' || value == ']' ||
          value == '}' || std::isspace(static_cast<unsigned char>(value)))
        break;
      ++end;
    }
    const std::string reference = text.substr(found, end - found);
    if (std::ranges::find(references, reference) == references.end())
      references.push_back(reference);
    cursor = end;
  }
  return references;
}

std::optional<std::filesystem::path>
optionalPath(const nlohmann::json &document, const char *field,
             const std::filesystem::path &base) {
  if (!document.contains(field) || !document[field].is_string() ||
      document[field].get<std::string>().empty())
    return std::nullopt;
  return base / document[field].get<std::string>();
}

void graphVisit(const AssetRegistry &registry, const AssetManifest &asset,
                std::set<std::string> &visiting, std::set<std::string> &visited,
                Diagnostics &diagnostics) {
  if (visited.contains(asset.id))
    return;
  if (!visiting.insert(asset.id).second) {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_DEPENDENCY_CYCLE",
         .message = "Asset dependency cycle includes " + asset.id,
         .path = asset.manifestPath.string(),
         .suggestion = "Remove a dependency edge from the "
                       "cycle."});
    return;
  }
  for (const std::string &dependencyId : asset.dependencies) {
    const AssetManifest *dependency = findAsset(registry, dependencyId);
    if (dependency == nullptr) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "ASSET_DEPENDENCY_NOT_FOUND",
           .message = "Asset dependency was not found: " + dependencyId,
           .path = asset.manifestPath.string(),
           .suggestion = "Import the dependency or correct "
                         "its stable asset ID."});
      continue;
    }
    graphVisit(registry, *dependency, visiting, visited, diagnostics);
  }
  visiting.erase(asset.id);
  visited.insert(asset.id);
}

} // namespace

std::optional<AssetManifest>
loadAssetManifest(const std::filesystem::path &manifestPath,
                  Diagnostic *diagnostic) {
  if (!std::filesystem::exists(manifestPath)) {
    if (diagnostic != nullptr)
      *diagnostic = {.severity = Severity::Error,
                     .code = "ASSET_MANIFEST_NOT_FOUND",
                     .message = "Asset manifest does not exist.",
                     .path = manifestPath.string(),
                     .suggestion = "Pass a valid .asset.json manifest path."};
    return std::nullopt;
  }
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(readFile(manifestPath));
  } catch (const nlohmann::json::exception &error) {
    if (diagnostic != nullptr)
      *diagnostic = {.severity = Severity::Error,
                     .code = "ASSET_MANIFEST_INVALID",
                     .message = error.what(),
                     .path = manifestPath.string(),
                     .suggestion = "Fix the asset manifest JSON."};
    return std::nullopt;
  }
  try {
    const std::string id = document.value("id", "");
    const std::string type = document.value("type", "");
    const bool animation = type == "ImageAnimation2D";
    if (id.empty() || type.empty() ||
        (animation
             ? !document.value("sources", nlohmann::json::array()).is_array()
             : !document.contains("source"))) {
      if (diagnostic != nullptr)
        *diagnostic = {.severity = Severity::Error,
                       .code = "ASSET_MANIFEST_INVALID",
                       .message =
                           "Asset manifest must include id, type, and "
                           "source or an ImageAnimation2D sources array.",
                       .path = manifestPath.string(),
                       .suggestion = "Add the required manifest fields."};
      return std::nullopt;
    }

    const auto base = manifestPath.parent_path();
    std::vector<std::filesystem::path> sources;
    if (animation) {
      for (const auto &source : document["sources"])
        if (source.is_string())
          sources.push_back(base / source.get<std::string>());
    } else if (document["source"].is_string()) {
      sources.push_back(base / document["source"].get<std::string>());
    }
    if (sources.empty())
      return std::nullopt;

    std::vector<std::filesystem::path> expandedSources = sources;
    std::set<std::filesystem::path> seen(sources.begin(), sources.end());
    for (const auto &sourcePath : sources) {
      const auto referenced = assets::collectReferencedSourceFiles(sourcePath);
      for (const auto &path : referenced)
        if (seen.insert(path).second)
          expandedSources.push_back(path);
    }
    sources = std::move(expandedSources);

    AssetManifest manifest;
    manifest.formatVersion = document.value("format_version", 1);
    manifest.id = id;
    manifest.type = type;
    manifest.importer = document.value("importer", "");
    manifest.importerVersion = document.value("importer_version", 0);
    manifest.sourceHash = document.value("source_hash", "");
    manifest.dependencies =
        document.value("dependencies", std::vector<std::string>{});
    manifest.settingsJson =
        document.value("settings", nlohmann::json::object()).dump();
    manifest.attribution = document.value("attribution", "");
    manifest.manifestPath = manifestPath;
    manifest.sourcePath = sources.front();
    manifest.sourcePaths = std::move(sources);
    manifest.texturePath = optionalPath(document, "texture", base);
    manifest.atlasPath = optionalPath(document, "atlas", base);
    manifest.generatedOutputPath =
        optionalPath(document, "generated_output", base);
    manifest.licensePath = optionalPath(document, "license", base);
    return manifest;
  } catch (const nlohmann::json::exception &error) {
    if (diagnostic != nullptr)
      *diagnostic = {.severity = Severity::Error,
                     .code = "ASSET_MANIFEST_INVALID",
                     .message = error.what(),
                     .path = manifestPath.string(),
                     .suggestion = "Fix the asset manifest field types."};
    return std::nullopt;
  }
}

AssetRegistry loadAssetRegistry(const std::filesystem::path &projectDirectory) {
  AssetRegistry registry{.projectDirectory = projectDirectory};
  const auto assetsDirectory = projectDirectory / "assets";
  if (!std::filesystem::exists(assetsDirectory))
    return registry;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(assetsDirectory)) {
    if (!entry.is_regular_file() ||
        !entry.path().filename().string().ends_with(".asset.json"))
      continue;
    Diagnostic diagnostic;
    auto manifest = loadAssetManifest(entry.path(), &diagnostic);
    if (manifest)
      registry.assets.push_back(std::move(*manifest));
    else
      registry.diagnostics.push_back(std::move(diagnostic));
  }
  std::ranges::sort(registry.assets, {}, &AssetManifest::id);
  return registry;
}

const AssetManifest *findAsset(const AssetRegistry &registry,
                               const std::string &id) {
  const auto found =
      std::ranges::lower_bound(registry.assets, id, {}, &AssetManifest::id);
  return found != registry.assets.end() && found->id == id ? &*found : nullptr;
}

std::vector<const AssetManifest *>
assetDependencies(const AssetRegistry &registry, const AssetManifest &asset,
                  Diagnostics *diagnostics) {
  std::vector<const AssetManifest *> result;
  std::set<std::string> visited;
  const auto visit = [&](const auto &self,
                         const AssetManifest &current) -> void {
    for (const auto &id : current.dependencies) {
      if (!visited.insert(id).second)
        continue;
      const auto *dependency = findAsset(registry, id);
      if (dependency == nullptr) {
        if (diagnostics != nullptr)
          diagnostics->push_back(
              {.severity = Severity::Error,
               .code = "ASSET_DEPENDENCY_NOT_FOUND",
               .message = "Asset dependency was not found: " + id,
               .path = current.manifestPath.string()});
        continue;
      }
      self(self, *dependency);
      result.push_back(dependency);
    }
  };
  visit(visit, asset);
  std::ranges::sort(result, {}, &AssetManifest::id);
  return result;
}

Diagnostics validateAssetRegistry(const AssetRegistry &registry) {
  Diagnostics diagnostics = registry.diagnostics;
  std::unordered_map<std::string, const AssetManifest *> ids;
  for (const AssetManifest &asset : registry.assets) {
    if (!ids.emplace(asset.id, &asset).second)
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_DUPLICATE_ID",
                             .message = "Duplicate asset ID: " + asset.id,
                             .path = asset.manifestPath.string(),
                             .suggestion = "Assign one stable ID per asset."});
    for (const auto &source : asset.sourcePaths)
      if (!std::filesystem::is_regular_file(source))
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_SOURCE_NOT_FOUND",
                               .message = "Asset source file does not exist.",
                               .path = source.string()});
    if (!asset.sourceHash.empty()) {
      const auto actual = assets::hashFiles(asset.sourcePaths);
      if (actual && *actual != asset.sourceHash)
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "ASSET_SOURCE_STALE",
             .message = "Asset source changed since import: " + asset.id,
             .path = asset.manifestPath.string(),
             .suggestion = "Run demi asset reimport on this "
                           "manifest."});
    }
    if (asset.generatedOutputPath &&
        !std::filesystem::is_regular_file(*asset.generatedOutputPath))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_GENERATED_OUTPUT_MISSING",
                             .message = "Generated asset output is missing.",
                             .path = asset.generatedOutputPath->string(),
                             .suggestion = "Run demi asset reimport."});
    else if (asset.generatedOutputPath && asset.sourcePaths.size() == 1) {
      const auto sourceHash = assets::hashFile(asset.sourcePath);
      const auto generatedHash = assets::hashFile(*asset.generatedOutputPath);
      if (sourceHash && generatedHash && *sourceHash != *generatedHash)
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "ASSET_GENERATED_OUTPUT_STALE",
             .message = "Generated output is stale: " + asset.id,
             .path = asset.generatedOutputPath->string(),
             .suggestion = "Run demi asset reimport."});
    }
    if (asset.texturePath &&
        !std::filesystem::is_regular_file(*asset.texturePath))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_TEXTURE_NOT_FOUND",
                             .message = "Asset texture file does not exist.",
                             .path = asset.texturePath->string()});
    if (asset.atlasPath && !std::filesystem::is_regular_file(*asset.atlasPath))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_ATLAS_NOT_FOUND",
                             .message = "Asset atlas file does not exist.",
                             .path = asset.atlasPath->string()});
    if (asset.licensePath &&
        !std::filesystem::is_regular_file(*asset.licensePath))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_LICENSE_NOT_FOUND",
                             .message = "Asset license file does not exist.",
                             .path = asset.licensePath->string()});
    if (asset.importerVersion > 0) {
      const auto importer = assets::importerFor(asset.sourcePath, asset.type);
      if (!importer)
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_FORMAT_UNSUPPORTED",
                               .message = "No importer supports this format.",
                               .path = asset.sourcePath.string()});
      else if (asset.importerVersion < importer->version)
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_IMPORTER_STALE",
                               .message = "Asset was generated by an older "
                                          "importer version.",
                               .path = asset.manifestPath.string(),
                               .suggestion = "Run demi asset reimport."});
    }
  }
  std::set<std::string> visiting;
  std::set<std::string> visited;
  for (const AssetManifest &asset : registry.assets)
    graphVisit(registry, asset, visiting, visited, diagnostics);
  return diagnostics;
}

std::vector<std::string> extractAssetReferences(const std::string &text) {
  return extractReferencesWithPrefix(text, "asset://");
}

std::vector<std::string> extractScriptReferences(const std::string &text) {
  return extractReferencesWithPrefix(text, "script://");
}

std::filesystem::path
resolveScriptReference(const std::filesystem::path &projectDirectory,
                       const std::string &reference) {
  constexpr std::string_view prefix = "script://";
  return reference.starts_with(prefix)
             ? projectDirectory / reference.substr(prefix.size())
             : projectDirectory / reference;
}

} // namespace demi
