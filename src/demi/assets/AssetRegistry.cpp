#include "demi/assets/AssetRegistry.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/GeneratedAtlasCooker.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/network/NetworkContract.h"

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
    const bool hasPipelineMetadata =
        document.contains("format_version") &&
        document["format_version"].is_number_integer() &&
        document.contains("importer") && document["importer"].is_string() &&
        !document["importer"].get<std::string>().empty() &&
        document.contains("importer_version") &&
        document["importer_version"].is_number_integer() &&
        document["importer_version"].get<int>() > 0 &&
        document.contains("source_hash") &&
        document["source_hash"].is_string() &&
        !document["source_hash"].get<std::string>().empty() &&
        document.contains("dependencies") &&
        document["dependencies"].is_array();
    if (!hasPipelineMetadata) {
      if (diagnostic != nullptr)
        *diagnostic = {
            .severity = Severity::Error,
            .code = "ASSET_MANIFEST_OUTDATED",
            .message = "Asset manifest is missing current pipeline metadata.",
            .path = manifestPath.string(),
            .suggestion = "Run demi asset reimport on this manifest."};
      return std::nullopt;
    }
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
    if (const auto settings = document.find("settings");
        settings != document.end() && settings->is_object()) {
      manifest.textureSettings.filter = settings->value("filter", "");
      manifest.textureSettings.wrap = settings->value("wrap", "clamp");
      manifest.textureSettings.mipmaps = settings->value("mipmaps", false);
    }
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
    if (!asset.textureSettings.filter.empty() &&
        asset.textureSettings.filter != "nearest" &&
        asset.textureSettings.filter != "bilinear" &&
        asset.textureSettings.filter != "trilinear")
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_TEXTURE_FILTER_INVALID",
                             .message = "Unsupported texture filter setting.",
                             .path = asset.manifestPath.string(),
                             .suggestion = "Use nearest, bilinear, or "
                                           "trilinear."});
    if (asset.type == "Model3D") {
      const nlohmann::json settings =
          nlohmann::json::parse(asset.settingsJson, nullptr, false);
      static_cast<void>(assets::parseModelImportProfile(
          settings.is_object() ? settings : nlohmann::json::object(),
          &diagnostics, asset.manifestPath.string()));
    }
    if (asset.type == "Collider3D") {
      try {
        const nlohmann::json collider =
            nlohmann::json::parse(readFile(asset.manifestPath));
        if (const auto generation = collider.find("generation");
            generation != collider.end() && generation->is_object()) {
          const std::string sourceId = generation->value("source_asset", "");
          const std::string sourceHash = generation->value("source_hash", "");
          const AssetManifest *source = findAsset(registry, sourceId);
          if (source == nullptr || source->type != "Model3D")
            diagnostics.push_back(
                {.severity = Severity::Error,
                 .code = "COLLIDER_SOURCE_ASSET_NOT_FOUND",
                 .message =
                     "Generated collider source asset is missing: " + sourceId,
                 .path = asset.manifestPath.string()});
          else if (sourceHash.empty() || sourceHash != source->sourceHash)
            diagnostics.push_back(
                {.severity = Severity::Error,
                 .code = "COLLIDER_GENERATION_STALE",
                 .message = "Collider generation inputs no longer match "
                            "the source model.",
                 .path = asset.manifestPath.string(),
                 .suggestion = "Run demi asset collider again."});
        }
      } catch (const nlohmann::json::exception &exception) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "COLLIDER_MANIFEST_INVALID",
                               .message = exception.what(),
                               .path = asset.manifestPath.string()});
      }
    }
    if (asset.textureSettings.wrap != "repeat" &&
        asset.textureSettings.wrap != "clamp" &&
        asset.textureSettings.wrap != "mirror")
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_TEXTURE_WRAP_INVALID",
                             .message = "Unsupported texture wrap setting.",
                             .path = asset.manifestPath.string(),
                             .suggestion = "Use repeat, clamp, or mirror."});
    if (asset.type == "Material") {
      if (const auto material =
              assets::loadMaterialAsset(asset.sourcePath, &diagnostics)) {
        for (const std::string *reference :
             {&material->shader, &material->fallback}) {
          if (!reference->starts_with("asset://"))
            continue;
          const AssetManifest *shader = findAsset(registry, *reference);
          if (shader == nullptr || shader->type != "Shader")
            diagnostics.push_back(
                {.severity = Severity::Error,
                 .code = "MATERIAL_SHADER_NOT_FOUND",
                 .message = "Material shader reference does not resolve to a "
                            "Shader asset: " +
                            *reference,
                 .path = asset.manifestPath.string(),
                 .suggestion =
                     "Import the shader and add it to dependencies."});
        }
      }
    } else if (asset.type == "Shader") {
      static_cast<void>(
          assets::loadShaderAsset(asset.sourcePath, &diagnostics));
    } else if (asset.type == "RenderTarget")
      (void)assets::loadRenderTargetAsset(asset.sourcePath, &diagnostics);
    if (asset.type == "Model3D") {
      const nlohmann::json settings =
          nlohmann::json::parse(asset.settingsJson, nullptr, false);
      const auto animations =
          settings.is_object() ? settings.find("animations") : settings.end();
      if (animations != settings.end() && animations->is_object()) {
        const auto clips = animations->find("clips");
        if (clips != animations->end() && clips->is_array()) {
          std::set<std::string> names;
          std::string skeleton;
          for (const auto &clip : *clips) {
            const std::string name =
                clip.is_object() ? clip.value("name", "") : "";
            const std::string clipSkeleton =
                clip.is_object() ? clip.value("skeleton", "") : "";
            if (name.empty())
              diagnostics.push_back(
                  {.severity = Severity::Error,
                   .code = "ANIMATION_CLIP_NAME_MISSING",
                   .message = "A model animation clip has no stable name.",
                   .path = asset.manifestPath.string(),
                   .suggestion = "Name every imported clip in "
                                 "settings.animations.clips."});
            else if (!names.insert(name).second)
              diagnostics.push_back(
                  {.severity = Severity::Error,
                   .code = "ANIMATION_CLIP_NAME_DUPLICATE",
                   .message = "Duplicate model animation clip name: " + name,
                   .path = asset.manifestPath.string()});
            if (!clipSkeleton.empty() && skeleton.empty())
              skeleton = clipSkeleton;
            else if (!clipSkeleton.empty() && clipSkeleton != skeleton)
              diagnostics.push_back(
                  {.severity = Severity::Error,
                   .code = "ANIMATION_SKELETON_INCOMPATIBLE",
                   .message =
                       "Model animation clips reference different skeletons.",
                   .path = asset.manifestPath.string(),
                   .suggestion =
                       "Retarget clips to one skeleton before importing."});
          }
        }
      }
    }
    if (asset.type == "AudioClip") {
      const nlohmann::json settings =
          nlohmann::json::parse(asset.settingsJson, nullptr, false);
      if (settings.is_object() && settings.contains("streaming") &&
          !settings["streaming"].is_boolean())
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "AUDIO_STREAMING_SETTING_INVALID",
             .message = "Audio streaming setting must be boolean.",
             .path = asset.manifestPath.string()});
    }
    if (asset.type == "TextureAtlas2D" || asset.type == "FontAtlas2D") {
      Diagnostics atlasDiagnostics =
          assets::validateGeneratedAtlasManifest(asset, registry);
      diagnostics.insert(diagnostics.end(), atlasDiagnostics.begin(),
                         atlasDiagnostics.end());
    }
  }
  Diagnostics dataDiagnostics = assets::validateDataAssets(registry);
  diagnostics.insert(diagnostics.end(), dataDiagnostics.begin(),
                     dataDiagnostics.end());
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "NetworkContract")
      continue;
    const runtime::NetworkContractLoadResult contract =
        runtime::loadNetworkContract(registry, asset.id);
    diagnostics.insert(diagnostics.end(), contract.diagnostics.begin(),
                       contract.diagnostics.end());
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
