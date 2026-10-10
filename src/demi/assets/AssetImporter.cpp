#include "demi/assets/AssetImporter.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/assets/DataAssetContent.h"
#include "demi/assets/DataDocument.h"
#include "demi/filesystem/AtomicTextFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace demi::assets {
namespace {

std::string idPath(std::string id) {
  constexpr std::string_view prefix = "asset://";
  if (id.starts_with(prefix))
    id.erase(0, prefix.size());
  std::ranges::replace(id, ':', '_');
  return id;
}

bool copyFile(const std::filesystem::path &source,
              const std::filesystem::path &target, std::string &error) {
  std::error_code code;
  std::filesystem::create_directories(target.parent_path(), code);
  if (!code && std::filesystem::weakly_canonical(source, code) !=
                   std::filesystem::weakly_canonical(target, code))
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing,
        code);
  if (code) {
    error = "Could not copy asset source: " + code.message();
    return false;
  }
  return true;
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

bool writeJson(const std::filesystem::path &path,
               const nlohmann::json &document) {
  std::error_code error;
  return atomicWriteText(path, document.dump(2) + '\n', error);
}

DataAssetContentResult inspectSource(const std::filesystem::path &source,
                                     const std::string_view contentType,
                                     const AssetRegistry &registry,
                                     const std::string_view assetId) {
  if (contentType.empty()) {
    return {.diagnostics = {{.severity = Severity::Error,
                             .code = "DATA_CONTENT_TYPE_MISSING",
                             .message = "DataAsset settings require content_type.",
                             .path = source.string()}}};
  }
  const DataDocumentResult parsed = loadDataDocument(source);
  DataAssetContentResult result{.diagnostics = parsed.diagnostics};
  if (!parsed.document || hasErrors(result.diagnostics))
    return result;
  const DataValue *version = parsed.document->root().find("format_version");
  if (version == nullptr || !version->isInteger() ||
      std::get<std::int64_t>(version->value) !=
          dataAssetContentFormatVersion(contentType)) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "DATA_FORMAT_VERSION_UNSUPPORTED",
         .message = "This data content requires format_version " +
                    std::to_string(dataAssetContentFormatVersion(contentType)) +
                    ".",
         .path = source.string() + "#/format_version"});
    return result;
  }
  DataAssetContentResult content =
      inspectDataAssetContent(contentType, *parsed.document, registry, assetId);
  result.diagnostics.insert(result.diagnostics.end(),
                            content.diagnostics.begin(),
                            content.diagnostics.end());
  result.dependencies = std::move(content.dependencies);
  return result;
}

void addDependencies(nlohmann::json &manifest,
                     const std::vector<std::string> &inferred) {
  std::vector<std::string> dependencies =
      manifest.value("dependencies", std::vector<std::string>{});
  dependencies.insert(dependencies.end(), inferred.begin(), inferred.end());
  std::ranges::sort(dependencies);
  dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                     dependencies.end());
  manifest["dependencies"] = std::move(dependencies);
}

std::optional<std::filesystem::path>
projectDirectoryFor(const std::filesystem::path &manifestPath) {
  std::optional<std::filesystem::path> candidate;
  for (auto directory = manifestPath.parent_path(); !directory.empty();
       directory = directory.parent_path()) {
    if (directory.filename() == "assets") {
      candidate = directory.parent_path();
      if (std::filesystem::is_regular_file(*candidate / "demi.project.json"))
        return candidate;
    }
    if (directory == directory.parent_path())
      break;
  }
  return candidate;
}

} // namespace

std::optional<ImporterDescriptor>
importerFor(const std::filesystem::path &source, const std::string &type) {
  std::string defaultImporter;
  if (type.empty()) {
    const std::string extension = source.extension().string();
    if (source.filename().string().ends_with(".collider.json"))
      defaultImporter = "collider-shape";
    else if (source.filename().string().ends_with(".terrain.json"))
      defaultImporter = "terrain_heightfield";
    else if (extension == ".json")
      defaultImporter = "json_data";
    else if (extension == ".gltf" || extension == ".glb")
      defaultImporter = "gltf-model";
  }
  return builtinImporterRegistry().select(source, type, defaultImporter);
}

AssetImportResult importAsset(const AssetImportRequest &request) {
  AssetImportResult result;
  if (!request.id.starts_with("asset://") || request.id.size() <= 8) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORT_INVALID_ID",
         .message = "Imported assets require a non-empty asset:// ID.",
         .path = request.id});
    return result;
  }
  if (!std::filesystem::is_regular_file(request.source)) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_IMPORT_SOURCE_NOT_FOUND",
                                  .message = "Import source does not exist.",
                                  .path = request.source.string()});
    return result;
  }
  const auto descriptor =
      request.importer.empty()
          ? importerFor(request.source, request.type)
          : builtinImporterRegistry().select(request.source, request.type,
                                             request.importer);
  if (!descriptor) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORTER_NOT_FOUND",
         .message = "No importer supports this source format.",
         .path = request.source.string(),
         .suggestion =
             "Use PNG/JPEG/SVG/GIF, WAV/OGG/MP3/FLAC, TTF/OTF, "
             "glTF/GLB/OBJ/IQM/M3D, MP4/WebM/MOV, or add an importer."});
    return result;
  }
  std::vector<std::string> terrainDependencies;
  if (descriptor->name == "terrain_heightfield") {
    const auto importer = builtinImporterRegistry().create(descriptor->name);
    const ImportExecutionResult execution = importer->import({
        .source = request.source,
        .assetId = request.id,
        .assetType = "Terrain"});
    if (hasErrors(execution.diagnostics)) {
      result.diagnostics = execution.diagnostics;
      return result;
    }
    terrainDependencies = execution.dependencies;
  }

  const AssetRegistry registry = loadAssetRegistry(request.projectDirectory);
  if (findAsset(registry, request.id) != nullptr) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_IMPORT_CONFLICT",
                                  .message = "Asset ID already exists.",
                                  .path = request.id,
                                  .suggestion = "Use demi asset reimport on "
                                                "the existing manifest."});
    return result;
  }

  const auto relativeId = std::filesystem::path(idPath(request.id));
  const auto assetDirectory = request.projectDirectory / "assets" / relativeId;
  const auto sourceTarget = assetDirectory / request.source.filename();
  result.manifestPath =
      assetDirectory / (request.source.stem().string() + ".asset.json");
  if (std::filesystem::exists(result.manifestPath)) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_IMPORT_CONFLICT",
                                  .message = "Asset manifest already exists.",
                                  .path = result.manifestPath.string(),
                                  .suggestion = "Use demi asset reimport."});
    return result;
  }
  const std::string importedType =
      request.type.empty() ? descriptor->assetTypes.front() : request.type;
  const std::string contentType = request.dataContentType.value_or("data");
  std::vector<std::string> dataDependencies;
  if (importedType == "DataAsset") {
    const DataAssetContentResult inspected =
        inspectSource(request.source, contentType, registry, request.id);
    if (hasErrors(inspected.diagnostics)) {
      result.diagnostics = inspected.diagnostics;
      return result;
    }
    dataDependencies = inspected.dependencies;
  }
  std::string copyError;
  if (!copyFile(request.source, sourceTarget, copyError)) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_IMPORT_COPY_FAILED",
                                  .message = copyError,
                                  .path = sourceTarget.string()});
    return result;
  }
  for (const auto &referenced : collectReferencedSourceFiles(request.source)) {
    if (referenced == request.source)
      continue;
    if (!pathIsInside(request.source.parent_path(), referenced)) {
      result.diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "ASSET_IMPORT_UNSAFE_DEPENDENCY",
           .message = "Source references a file outside its directory.",
           .path = referenced.string()});
      return result;
    }
    const auto relative =
        std::filesystem::relative(referenced, request.source.parent_path());
    if (!copyFile(referenced, assetDirectory / relative, copyError)) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_COPY_FAILED",
                                    .message = copyError,
                                    .path = referenced.string()});
      return result;
    }
  }
  nlohmann::json manifest{
      {"format_version", 1},
      {"id", request.id},
      {"type",
       request.type.empty() ? descriptor->assetTypes.front() : request.type},
      {"source", sourceTarget.filename().generic_string()},
      {"importer", descriptor->name},
      {"importer_version", descriptor->version},
      {"source_hash", *hashFiles(collectReferencedSourceFiles(sourceTarget))},
      {"dependencies", terrainDependencies},
      {"settings", nlohmann::json::object()},
  };
  if (descriptor->copyToGeneratedOnImport) {
    const auto generatedTarget = request.projectDirectory / "generated" /
                                 "assets" / relativeId /
                                 request.source.filename();
    if (!copyFile(sourceTarget, generatedTarget, copyError)) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_GENERATE_FAILED",
                                    .message = copyError,
                                    .path = generatedTarget.string()});
      return result;
    }
    manifest["generated_output"] =
        std::filesystem::relative(generatedTarget, assetDirectory)
            .generic_string();
  }
  if (importedType == "Model3D")
    manifest["settings"]["model_import"] = modelImportProfileJson(
        request.modelProfile.value_or(modelImportPreset("static_prop")));
  if (importedType == "DataAsset")
    manifest["settings"]["content_type"] = contentType;
  if (importedType == "DataAsset")
    addDependencies(manifest, dataDependencies);
  if (request.license) {
    const auto licenseTarget = assetDirectory / request.license->filename();
    if (!copyFile(*request.license, licenseTarget, copyError)) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_LICENSE_FAILED",
                                    .message = copyError,
                                    .path = licenseTarget.string()});
      return result;
    }
    manifest["license"] = licenseTarget.filename().generic_string();
  }
  std::filesystem::create_directories(result.manifestPath.parent_path());
  if (!writeJson(result.manifestPath, manifest))
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_IMPORT_WRITE_FAILED",
                                  .message = "Could not write asset manifest.",
                                  .path = result.manifestPath.string()});
  return result;
}

AssetImportResult
registerGeneratedAsset(const GeneratedAssetRegistrationRequest &request) {
  AssetImportResult result;
  if (!request.id.starts_with("asset://") || request.id.size() <= 8) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORT_INVALID_ID",
         .message = "Generated assets require a non-empty asset:// ID.",
         .path = request.id});
    return result;
  }
  if (!std::filesystem::is_regular_file(request.source)) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORT_SOURCE_NOT_FOUND",
         .message = "Generated asset source does not exist.",
         .path = request.source.string()});
    return result;
  }

  const auto assetsDirectory = request.projectDirectory / "assets";
  if (!pathIsInside(assetsDirectory, request.source)) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_GENERATED_SOURCE_OUTSIDE_ASSETS",
         .message = "Generated asset sources must be inside the project's "
                    "assets directory.",
         .path = request.source.string(),
         .suggestion = "Generate the source under assets/generated."});
    return result;
  }

  const auto descriptor = importerFor(request.source, request.type);
  if (!descriptor) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORTER_NOT_FOUND",
         .message = "No importer supports this generated source format.",
         .path = request.source.string()});
    return result;
  }

  result.manifestPath = request.source.parent_path() /
                        (request.source.stem().string() + ".asset.json");
  nlohmann::json manifest = nlohmann::json::object();
  try {
    if (std::filesystem::exists(result.manifestPath)) {
      manifest = readJson(result.manifestPath);
      const std::string existingId = manifest.value("id", "");
      if (!existingId.empty() && existingId != request.id) {
        result.diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "ASSET_IMPORT_CONFLICT",
             .message = "Generated asset manifest already belongs to another "
                        "asset ID.",
             .path = result.manifestPath.string()});
        return result;
      }
    } else if (findAsset(loadAssetRegistry(request.projectDirectory),
                         request.id) != nullptr) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_CONFLICT",
                                    .message = "Asset ID already exists.",
                                    .path = request.id});
      return result;
    }
  } catch (const nlohmann::json::exception &error) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "ASSET_MANIFEST_INVALID",
                                  .message = error.what(),
                                  .path = result.manifestPath.string()});
    return result;
  }

  const auto sourceFiles = collectReferencedSourceFiles(request.source);
  for (const auto &sourceFile : sourceFiles) {
    if (!pathIsInside(request.source.parent_path(), sourceFile) &&
        sourceFile != request.source) {
      result.diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "ASSET_IMPORT_UNSAFE_DEPENDENCY",
           .message =
               "Generated source references a file outside its directory.",
           .path = sourceFile.string()});
      return result;
    }
    if (!std::filesystem::is_regular_file(sourceFile)) {
      result.diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "ASSET_SOURCE_NOT_FOUND",
           .message = "Could not read generated asset sources.",
           .path = sourceFile.string()});
      return result;
    }
  }

  manifest["format_version"] = 1;
  manifest["id"] = request.id;
  manifest["type"] =
      request.type.empty() ? descriptor->assetTypes.front() : request.type;
  manifest["source"] = request.source.filename().generic_string();
  manifest["importer"] = descriptor->name;
  manifest["importer_version"] = descriptor->version;
  manifest["source_hash"] = *hashFiles(sourceFiles);
  if (!manifest.contains("dependencies"))
    manifest["dependencies"] = nlohmann::json::array();
  if (!manifest.contains("settings"))
    manifest["settings"] = nlohmann::json::object();
  if (!manifest["settings"].is_object()) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "DATA_ASSET_SETTINGS_INVALID",
                                  .message = "Asset settings must be an object.",
                                  .path = result.manifestPath.string()});
    return result;
  }
  if ((request.type.empty() ? descriptor->assetTypes.front() : request.type) ==
          "DataAsset" &&
      !manifest["settings"].contains("content_type"))
    manifest["settings"]["content_type"] = "data";

  if (manifest["type"] == "DataAsset") {
    try {
      const std::string contentType =
          manifest["settings"].value("content_type", "data");
      const AssetRegistry registry = loadAssetRegistry(request.projectDirectory);
      const DataAssetContentResult inspected =
          inspectSource(request.source, contentType, registry, request.id);
      if (hasErrors(inspected.diagnostics)) {
        result.diagnostics = inspected.diagnostics;
        return result;
      }
      addDependencies(manifest, inspected.dependencies);
    } catch (const nlohmann::json::exception &failure) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "DATA_ASSET_SETTINGS_INVALID",
                                    .message = failure.what(),
                                    .path = result.manifestPath.string()});
      return result;
    }
  }

  if (!writeJson(result.manifestPath, manifest)) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORT_WRITE_FAILED",
         .message = "Could not write generated asset manifest.",
         .path = result.manifestPath.string()});
    return result;
  }
  result.diagnostics = reimportAsset(result.manifestPath);
  return result;
}

Diagnostics reimportAsset(const std::filesystem::path &manifestPath) {
  Diagnostics diagnostics;
  Diagnostic diagnostic;
  const auto manifest = loadAssetManifest(manifestPath, &diagnostic);
  if (!manifest) {
    diagnostics.push_back(std::move(diagnostic));
    return diagnostics;
  }
  const auto descriptor = importerFor(manifest->sourcePath, manifest->type);
  if (!descriptor) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_IMPORTER_NOT_FOUND",
                           .message = "No importer supports this asset source.",
                           .path = manifest->sourcePath.string()});
    return diagnostics;
  }
  const auto hash = hashFiles(manifest->sourcePaths);
  if (!hash) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_SOURCE_NOT_FOUND",
                           .message = "Could not read asset sources.",
                           .path = manifestPath.string()});
    return diagnostics;
  }
  try {
    std::vector<std::string> terrainDependencies;
    if (descriptor->name == "terrain_heightfield") {
      const auto importer = builtinImporterRegistry().create(descriptor->name);
      const ImportExecutionResult execution = importer->import({
          .source = manifest->sourcePath,
          .assetId = manifest->id,
          .assetType = "Terrain"});
      if (hasErrors(execution.diagnostics))
        return execution.diagnostics;
      terrainDependencies = execution.dependencies;
    }
    if (descriptor->name == "collider-shape") {
      std::string error;
      if (!loadColliderShapeAsset(manifest->sourcePath, error)) {
        diagnostics.push_back({.severity=Severity::Error, .code="COLLIDER_SHAPE_INVALID",
                               .message=error, .path=manifest->sourcePath.string()});
        return diagnostics;
      }
    }
    auto document = readJson(manifestPath);
    std::vector<std::string> dataDependencies;
    if (manifest->type == "DataAsset") {
      const auto settings = document.value("settings", nlohmann::json::object());
      if (!settings.is_object()) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "DATA_ASSET_SETTINGS_INVALID",
                               .message = "DataAsset settings must be an object.",
                               .path = manifestPath.string()});
        return diagnostics;
      }
      if (!settings.contains("content_type")) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "DATA_CONTENT_TYPE_MISSING",
                               .message = "DataAsset settings require content_type.",
                               .path = manifestPath.string()});
        return diagnostics;
      }
      const std::string contentType =
          settings.at("content_type").get<std::string>();
      const auto projectDirectory = projectDirectoryFor(manifestPath);
      if (!projectDirectory) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_IMPORT_PROJECT_NOT_FOUND",
                               .message = "DataAsset manifest must be below "
                                          "the project assets directory.",
                               .path = manifestPath.string()});
        return diagnostics;
      }
      const AssetRegistry registry = loadAssetRegistry(*projectDirectory);
      const DataAssetContentResult inspected = inspectSource(
          manifest->sourcePath, contentType, registry, manifest->id);
      if (hasErrors(inspected.diagnostics))
        return inspected.diagnostics;
      dataDependencies = inspected.dependencies;
    }
    document["format_version"] = 1;
    document["importer"] = descriptor->name;
    document["importer_version"] = descriptor->version;
    document["source_hash"] = *hash;
    if (descriptor->name == "terrain_heightfield")
      document["dependencies"] = terrainDependencies;
    if (manifest->type == "DataAsset")
      addDependencies(document, dataDependencies);
    if (!document.contains("dependencies"))
      document["dependencies"] = nlohmann::json::array();
    if (!document.contains("settings"))
      document["settings"] = nlohmann::json::object();
    if (descriptor->name == "collider-shape" ||
        descriptor->name == "terrain_heightfield") {
      // Migrate older mirror-only manifests; the source and stable ID stay put.
      // Leave the old cache file alone rather than deleting authoring data.
      document.erase("generated_output");
    } else if (manifest->generatedOutputPath) {
      std::string error;
      if (!copyFile(manifest->sourcePath, *manifest->generatedOutputPath,
                    error)) {
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "ASSET_IMPORT_GENERATE_FAILED",
             .message = error,
             .path = manifest->generatedOutputPath->string()});
        return diagnostics;
      }
    }
    if (!writeJson(manifestPath, document))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_IMPORT_WRITE_FAILED",
                             .message = "Could not update asset manifest.",
                             .path = manifestPath.string()});
  } catch (const std::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_MANIFEST_INVALID",
                           .message = error.what(),
                           .path = manifestPath.string()});
  }
  return diagnostics;
}

} // namespace demi::assets
