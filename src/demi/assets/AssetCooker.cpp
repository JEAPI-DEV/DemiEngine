#include "demi/assets/AssetCooker.h"

#include "demi/assets/AssetCookGraph.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporterRegistry.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/GeneratedAtlasCooker.h"
#include "demi/assets/PackageContent.h"
#include "demi/assets/RenderAsset.h"
#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace demi::assets {
namespace {

#ifndef DEMI_SHADERC_PATH
#define DEMI_SHADERC_PATH ""
#endif
#ifndef DEMI_BGFX_SHADER_INCLUDE_DIR
#define DEMI_BGFX_SHADER_INCLUDE_DIR ""
#endif

bool skippedRoot(const std::filesystem::path &relative) {
  if (relative.empty())
    return false;
  const std::string first = relative.begin()->string();
  return first == "assets" || first == "build" || first == "generated" ||
         first == ".git" || first == ".demi" || first == "saves" ||
         first == "tests";
}

std::filesystem::path
cookedRelativePath(const std::filesystem::path &source,
                   const std::filesystem::path &projectDirectory) {
  const auto installed = projectDirectory / ".demi/packages";
  if (pathIsInside(installed, source))
    return std::filesystem::path("packages") /
           std::filesystem::relative(source, installed);
  return std::filesystem::relative(source, projectDirectory);
}

void addProjectFiles(const std::filesystem::path &projectDirectory,
                     std::set<std::filesystem::path> &files,
                     Diagnostics &diagnostics) {
  std::error_code code;
  for (std::filesystem::recursive_directory_iterator
           iterator(projectDirectory, code),
       end;
       iterator != end; iterator.increment(code)) {
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_SCAN_FAILED",
                             .message = code.message(),
                             .path = projectDirectory.string()});
      return;
    }
    const auto relative =
        std::filesystem::relative(iterator->path(), projectDirectory, code);
    if (skippedRoot(relative)) {
      if (iterator->is_directory())
        iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file())
      files.insert(iterator->path());
  }
}

void addAssetGroupFiles(const std::filesystem::path &projectDirectory,
                        std::set<std::filesystem::path> &files,
                        Diagnostics &diagnostics) {
  const auto assetsDirectory = projectDirectory / "assets";
  if (!std::filesystem::is_directory(assetsDirectory))
    return;

  std::error_code code;
  for (std::filesystem::recursive_directory_iterator
           iterator(assetsDirectory, code),
       end;
       iterator != end; iterator.increment(code)) {
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_ASSET_GROUP_SCAN_FAILED",
                             .message = code.message(),
                             .path = assetsDirectory.string(),
                             .suggestion = {}});
      return;
    }
    if (iterator->is_regular_file() &&
        iterator->path().filename().string().ends_with(".asset-group.json"))
      files.insert(iterator->path());
  }
}

std::string shellQuote(const std::filesystem::path &path) {
  std::string result = "'";
  for (const char character : path.string())
    result += character == '\'' ? "'\\''" : std::string(1, character);
  return result + "'";
}

struct ShaderVariant {
  const char *backend;
  const char *shadercPlatform;
  const char *profile;
};

std::vector<ShaderVariant> shaderVariants(const std::string &platform) {
  if (platform == "linux")
    return {{"vulkan", "linux", "spirv"}, {"opengl", "linux", "140"}};
  if (platform == "android")
    return {{"vulkan", "android", "spirv"}, {"opengles", "android", "300_es"}};
  return {};
}

std::string safeAssetName(std::string id) {
  for (char &character : id)
    if (!std::isalnum(static_cast<unsigned char>(character)) &&
        character != '-' && character != '_')
      character = '_';
  return id;
}

bool usesUnifiedShaderSource(const ShaderAsset &shader) {
  return shader.stages.vertex.extension() == ".sc" &&
         shader.stages.fragment.extension() == ".sc";
}

std::string normalizedSettingsFor(const AssetManifest &asset,
                                  const std::string &platform,
                                  Diagnostics &diagnostics) {
  try {
    nlohmann::json settings = nlohmann::json::parse(asset.settingsJson);
    if (const auto overrides = settings.find("platform_overrides");
        overrides != settings.end() && overrides->is_object()) {
      if (const auto selected = overrides->find(platform);
          selected != overrides->end() && selected->is_object())
        for (const auto &[name, value] : selected->items())
          settings[name] = value;
      settings.erase("platform_overrides");
    }
    return settings.dump();
  } catch (const nlohmann::json::exception &exception) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "COOK_ASSET_SETTINGS_INVALID",
                           .message = exception.what(),
                           .path = asset.manifestPath.string()});
    return "{}";
  }
}

std::optional<ImporterDescriptor>
importerDescriptorFor(const AssetManifest &asset,
                      const LockedPackageContent &packageContent,
                      const std::string &platform, Diagnostics &diagnostics) {
  if (auto builtin = builtinImporterRegistry().select(
          asset.sourcePath, asset.type, asset.importer))
    return builtin;
  for (const PackageExtensionRegistration &extension :
       packageContent.extensions) {
    if (!extension.importer || extension.importer->name != asset.importer)
      continue;
    const ImporterDescriptor &candidate = *extension.importer;
    const std::string sourceExtension = asset.sourcePath.extension().string();
    if (std::ranges::find(candidate.extensions, sourceExtension) ==
            candidate.extensions.end() ||
        std::ranges::find(candidate.assetTypes, asset.type) ==
            candidate.assetTypes.end() ||
        std::ranges::find(candidate.platforms, platform) ==
            candidate.platforms.end())
      break;
    return candidate;
  }
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = "COOK_IMPORTER_UNAVAILABLE",
       .message = "The asset's importer is not registered for this source, "
                  "type, and platform.",
       .path = asset.manifestPath.string(),
       .suggestion = "Install the locked package that provides the importer "
                     "or choose a supported importer."});
  return std::nullopt;
}

bool compileShaderStage(const CookRequest &request,
                        const std::filesystem::path &source,
                        const std::filesystem::path &varying,
                        const std::filesystem::path &output, const char *type,
                        const ShaderVariant &variant,
                        Diagnostics &diagnostics) {
  std::filesystem::create_directories(output.parent_path());
  std::ostringstream command;
  command << shellQuote(request.shaderCompiler) << " -f " << shellQuote(source)
          << " -o " << shellQuote(output) << " --type " << type
          << " --platform " << variant.shadercPlatform << " -p "
          << variant.profile << " --varyingdef " << shellQuote(varying)
          << " -i " << shellQuote(source.parent_path());
  if (!request.shaderIncludeDirectory.empty())
    command << " -i " << shellQuote(request.shaderIncludeDirectory);
  command << " --Werror";
  if (std::system(command.str().c_str()) == 0 &&
      std::filesystem::is_regular_file(output))
    return true;
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = "COOK_SHADER_COMPILE_FAILED",
       .message = "bgfx shaderc failed for the " + std::string(type) +
                  " stage targeting " + variant.backend + ".",
       .path = source.string(),
       .suggestion = "Run demi cook again and fix the shaderc diagnostics."});
  return false;
}

} // namespace

Diagnostics cookProject(const CookRequest &request) {
  Diagnostics diagnostics;
  if (request.platform != "linux" && request.platform != "linux_server" &&
      request.platform != "android") {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "COOK_PLATFORM_UNSUPPORTED",
         .message =
             "Cooking is not implemented for platform: " + request.platform,
         .path = request.projectFile.string(),
         .suggestion = "Use --platform linux, linux_server, or android."});
    return diagnostics;
  }
  const auto absoluteProject = std::filesystem::absolute(request.projectFile);
  CookRequest effectiveRequest = request;
  if (effectiveRequest.shaderCompiler.empty())
    effectiveRequest.shaderCompiler = DEMI_SHADERC_PATH;
  if (effectiveRequest.shaderIncludeDirectory.empty())
    effectiveRequest.shaderIncludeDirectory = DEMI_BGFX_SHADER_INCLUDE_DIR;
  const auto summary = validatePath(absoluteProject);
  diagnostics.insert(diagnostics.end(), summary.diagnostics.begin(),
                     summary.diagnostics.end());
  const auto projectDirectory = absoluteProject.parent_path();
  AssetRegistry registry = loadAssetRegistry(projectDirectory);
  LockedPackageContent packageContent =
      loadLockedPackageContent(projectDirectory, request.platform, &registry);
  diagnostics.insert(diagnostics.end(), packageContent.diagnostics.begin(),
                     packageContent.diagnostics.end());
  registry.assets.insert(registry.assets.end(), packageContent.assets.begin(),
                         packageContent.assets.end());
  std::ranges::sort(registry.assets, {}, &AssetManifest::id);
  if (hasErrors(diagnostics))
    return diagnostics;

  std::set<std::filesystem::path> files;
  addProjectFiles(projectDirectory, files, diagnostics);
  addAssetGroupFiles(projectDirectory, files, diagnostics);
  for (const auto &asset : registry.assets)
    for (const auto &file : collectAssetFiles(asset)) {
      if (!pathIsInside(projectDirectory, file))
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "COOK_EXTERNAL_ASSET_FILE",
                               .message = "Cooked assets must be inside the "
                                          "project directory.",
                               .path = file.string()});
      else
        files.insert(file);
    }
  for (const auto &file : packageContent.files)
    files.insert(file);
  if (hasErrors(diagnostics))
    return diagnostics;

  AssetCookGraph cookGraph;
  for (const AssetManifest &asset : registry.assets) {
    const auto descriptor = importerDescriptorFor(
        asset, packageContent, request.platform, diagnostics);
    std::vector<std::string> sourceHashes;
    for (const auto &source : asset.sourcePaths) {
      const auto hash = hashFile(source);
      if (hash)
        sourceHashes.push_back(*hash);
    }
    const int importerVersion =
        descriptor ? descriptor->version : asset.importerVersion;
    const int settingsSchemaVersion =
        descriptor ? descriptor->settingsSchemaVersion : 1;
    (void)cookGraph.addNode({.assetId = asset.id,
                             .importer = asset.importer,
                             .importerVersion = importerVersion,
                             .settingsSchemaVersion = settingsSchemaVersion,
                             .normalizedSettings = normalizedSettingsFor(
                                 asset, request.platform, diagnostics),
                             .sourceHashes = std::move(sourceHashes),
                             .dependencies = asset.dependencies,
                             .platform = request.platform,
                             .profile = "default",
                             .sourcePackage = asset.sourcePackage,
                             .packageContentHash = asset.packageContentHash},
                            &diagnostics);
  }
  (void)cookGraph.finalize(&diagnostics);
  if (hasErrors(diagnostics))
    return diagnostics;

  std::error_code code;
  std::filesystem::create_directories(request.outputDirectory, code);
  AssetCookCache cookCache(request.outputDirectory);
  std::map<std::string, AssetCookDecision> cookDecisions;
  std::map<std::filesystem::path, std::string> fileOwners;
  for (const AssetManifest &asset : registry.assets) {
    const std::string key = cookGraph.key(asset.id).value_or("");
    cookDecisions.emplace(asset.id, cookCache.inspect(asset.id, key));
    for (const auto &file : collectAssetFiles(asset))
      fileOwners.emplace(file, asset.id);
  }
  nlohmann::json cookedFiles = nlohmann::json::array();
  std::set<std::string> reportedFiles;
  const auto reportCookedFile = [&](const std::filesystem::path &target) {
    if (!std::filesystem::is_regular_file(target) ||
        !pathIsInside(request.outputDirectory, target))
      return;
    const auto relative =
        std::filesystem::relative(target, request.outputDirectory);
    if (relative.empty() || relative.begin()->string() == ".cook-cache" ||
        !reportedFiles.insert(relative.generic_string()).second)
      return;
    if (const auto hash = hashFile(target))
      cookedFiles.push_back(
          {{"path", relative.generic_string()}, {"hash", *hash}});
  };
  for (const auto &source : files) {
    const auto relative = cookedRelativePath(source, projectDirectory);
    const auto target = request.outputDirectory / relative;
    const auto owner = fileOwners.find(source);
    const bool isCacheHit =
        owner != fileOwners.end() && cookDecisions.at(owner->second).isCacheHit;
    std::filesystem::create_directories(target.parent_path(), code);
    if (!code && !isCacheHit)
      std::filesystem::copy_file(
          source, target, std::filesystem::copy_options::overwrite_existing,
          code);
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_COPY_FAILED",
                             .message = code.message(),
                             .path = source.string()});
      code.clear();
      continue;
    }
    reportCookedFile(target);
  }

  nlohmann::json shaderPrograms = nlohmann::json::array();
  nlohmann::json assetCook = nlohmann::json::array();
  nlohmann::json cacheReport = nlohmann::json::array();
  for (const AssetManifest &asset : registry.assets) {
    AssetCookDecision &decision = cookDecisions.at(asset.id);
    std::vector<std::string> currentSourceHashes;
    for (const auto &source : asset.sourcePaths) {
      const auto hash = hashFile(source);
      if (hash)
        currentSourceHashes.push_back(*hash);
    }
    std::ranges::sort(currentSourceHashes);
    if (currentSourceHashes != cookGraph.nodes().at(asset.id).sourceHashes) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_SOURCE_CHANGED_DURING_BUILD",
           .message = "Asset source changed while its cook was in progress.",
           .path = asset.manifestPath.string(),
           .suggestion = "Retry the cook after the source writer finishes."});
      continue;
    }
    if (!decision.isCacheHit) {
      for (const auto &source : collectAssetFiles(asset))
        decision.outputs.push_back(
            request.outputDirectory /
            cookedRelativePath(source, projectDirectory));
      GeneratedAtlasCookResult generated =
          cookGeneratedAtlas(asset, registry, request.outputDirectory);
      diagnostics.insert(diagnostics.end(), generated.diagnostics.begin(),
                         generated.diagnostics.end());
      decision.outputs.insert(decision.outputs.end(), generated.outputs.begin(),
                              generated.outputs.end());
      if (hasErrors(generated.diagnostics))
        continue;
      const Diagnostics cacheDiagnostics = cookCache.store(
          decision, asset.sourcePackage, asset.packageContentHash);
      diagnostics.insert(diagnostics.end(), cacheDiagnostics.begin(),
                         cacheDiagnostics.end());
    }
    for (const auto &outputPath : decision.outputs)
      reportCookedFile(outputPath);
    assetCook.push_back({{"asset", asset.id},
                         {"key", decision.key},
                         {"source_package", asset.sourcePackage},
                         {"package_content_hash", asset.packageContentHash}});
    cacheReport.push_back({{"asset", asset.id},
                           {"key", decision.key},
                           {"cache_hit", decision.isCacheHit},
                           {"reason", decision.reason}});
  }
  nlohmann::json extensions = nlohmann::json::array();
  for (const PackageExtensionRegistration &extension :
       packageContent.extensions)
    extensions.push_back({{"package", extension.package},
                          {"id", extension.id},
                          {"kind", extension.kind},
                          {"entry", extension.entry.generic_string()}});
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "Shader")
      continue;
    const auto shader = loadShaderAsset(asset.sourcePath, &diagnostics);
    if (!shader || !usesUnifiedShaderSource(*shader))
      continue;
    if (!shader->varyingDefinition) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_SHADER_VARYING_MISSING",
           .message = "Unified .sc shaders require a varying definition file.",
           .path = asset.sourcePath.string(),
           .suggestion =
               "Add \"varying\": \"varying.def.sc\" to the shader asset."});
      continue;
    }
    if (effectiveRequest.shaderCompiler.empty() ||
        !std::filesystem::is_regular_file(effectiveRequest.shaderCompiler)) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_SHADER_COMPILER_NOT_FOUND",
           .message = "The host bgfx shaderc executable is unavailable.",
           .path = effectiveRequest.shaderCompiler.string(),
           .suggestion = "Build the shaderc target before cooking shaders."});
      continue;
    }
    for (const ShaderVariant &variant : shaderVariants(request.platform)) {
      const auto directory = request.outputDirectory / "generated/shaders" /
                             safeAssetName(asset.id) / variant.backend;
      const auto vertex = directory / "vertex.bin";
      const auto fragment = directory / "fragment.bin";
      if (!compileShaderStage(effectiveRequest, shader->stages.vertex,
                              *shader->varyingDefinition, vertex, "vertex",
                              variant, diagnostics) ||
          !compileShaderStage(effectiveRequest, shader->stages.fragment,
                              *shader->varyingDefinition, fragment, "fragment",
                              variant, diagnostics))
        continue;
      const auto vertexRelative =
          std::filesystem::relative(vertex, request.outputDirectory);
      const auto fragmentRelative =
          std::filesystem::relative(fragment, request.outputDirectory);
      reportCookedFile(vertex);
      reportCookedFile(fragment);
      shaderPrograms.push_back(
          {{"asset", asset.id},
           {"backend", variant.backend},
           {"vertex", vertexRelative.generic_string()},
           {"fragment", fragmentRelative.generic_string()}});
    }
  }
  if (hasErrors(diagnostics))
    return diagnostics;
  const nlohmann::json manifest{
      {"format_version", 1},
      {"platform", request.platform},
      {"project", absoluteProject.filename().generic_string()},
      {"files", std::move(cookedFiles)},
      {"assets", std::move(assetCook)},
      {"package_extensions", std::move(extensions)},
      {"shader_programs", std::move(shaderPrograms)},
  };
  std::ofstream output(request.outputDirectory / "cook.manifest.json");
  if (!output)
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "COOK_MANIFEST_WRITE_FAILED",
                           .message = "Could not write cook manifest.",
                           .path = request.outputDirectory.string()});
  else
    output << manifest.dump(2) << '\n';
  std::ofstream reportOutput(request.outputDirectory /
                             ".cook-cache/last-report.json");
  if (reportOutput)
    reportOutput << nlohmann::json{{"format_version", 1},
                                   {"platform", request.platform},
                                   {"assets", std::move(cacheReport)}}
                        .dump(2)
                 << '\n';
  return diagnostics;
}

} // namespace demi::assets
