#include "demi/assets/AssetImporter.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace demi::assets {
namespace {

std::string lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

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
  std::ofstream output(path);
  if (!output)
    return false;
  output << document.dump(2) << '\n';
  return true;
}

} // namespace

std::optional<ImporterDescriptor>
importerFor(const std::filesystem::path &source, const std::string &type) {
  static const std::unordered_map<std::string, ImporterDescriptor> importers{
      {".png", {"image", 1, "Texture2D"}},
      {".jpg", {"image", 1, "Texture2D"}},
      {".jpeg", {"image", 1, "Texture2D"}},
      {".bmp", {"image", 1, "Texture2D"}},
      {".tga", {"image", 1, "Texture2D"}},
      {".qoi", {"image", 1, "Texture2D"}},
      {".ppm", {"image", 1, "Texture2D"}},
      {".svg", {"svg", 1, "Icon2D"}},
      {".gif", {"gif", 1, "GifAnimation2D"}},
      {".wav", {"audio", 1, "AudioClip"}},
      {".ogg", {"audio", 1, "AudioClip"}},
      {".mp3", {"audio", 1, "AudioClip"}},
      {".flac", {"audio", 1, "AudioClip"}},
      {".gltf", {"gltf-model", 1, "Model3D"}},
      {".glb", {"gltf-model", 1, "Model3D"}},
      {".obj", {"model", 1, "Model3D"}},
      {".iqm", {"model", 1, "Model3D"}},
      {".m3d", {"model", 1, "Model3D"}},
      {".mp4", {"video", 1, "VideoClip"}},
      {".webm", {"video", 1, "VideoClip"}},
      {".mov", {"video", 1, "VideoClip"}},
      {".json", {"json_data", 1, "DataAsset"}},
  };
  const auto found = importers.find(lower(source.extension().string()));
  if (found == importers.end())
    return std::nullopt;
  ImporterDescriptor descriptor = found->second;
  if (!type.empty()) {
    descriptor.assetType = type;
    if (lower(source.extension().string()) == ".json" && type != "DataAsset" &&
        type != "DataSchema")
      descriptor.name = "json-data";
  }
  return descriptor;
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
  const auto descriptor = importerFor(request.source, request.type);
  if (!descriptor) {
    result.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "ASSET_IMPORTER_NOT_FOUND",
         .message = "No importer supports this source format.",
         .path = request.source.string(),
         .suggestion =
             "Use PNG/JPEG/SVG/GIF, WAV/OGG/MP3/FLAC, "
             "glTF/GLB/OBJ/IQM/M3D, MP4/WebM/MOV, or add an importer."});
    return result;
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
  nlohmann::json manifest{
      {"format_version", 1},
      {"id", request.id},
      {"type", descriptor->assetType},
      {"source", sourceTarget.filename().generic_string()},
      {"importer", descriptor->name},
      {"importer_version", descriptor->version},
      {"source_hash", *hashFiles(collectReferencedSourceFiles(sourceTarget))},
      {"dependencies", nlohmann::json::array()},
      {"generated_output",
       std::filesystem::relative(generatedTarget, assetDirectory)
           .generic_string()},
      {"settings", nlohmann::json::object()},
  };
  if (descriptor->assetType == "DataAsset")
    manifest["settings"]["content_type"] = "data";
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
  manifest["type"] = descriptor->assetType;
  manifest["source"] = request.source.filename().generic_string();
  if (!manifest.contains("dependencies"))
    manifest["dependencies"] = nlohmann::json::array();
  if (!manifest.contains("settings"))
    manifest["settings"] = nlohmann::json::object();
  if (descriptor->assetType == "DataAsset" &&
      !manifest["settings"].contains("content_type"))
    manifest["settings"]["content_type"] = "data";

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
  const auto manifest =
      loadAssetManifestForMigration(manifestPath, &diagnostic);
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
    auto document = readJson(manifestPath);
    document["format_version"] = 1;
    document["importer"] = descriptor->name;
    document["importer_version"] = descriptor->version;
    document["source_hash"] = *hash;
    if (!document.contains("dependencies"))
      document["dependencies"] = nlohmann::json::array();
    if (!document.contains("settings"))
      document["settings"] = nlohmann::json::object();
    if (manifest->generatedOutputPath) {
      std::string error;
      if (!copyFile(manifest->sourcePath, *manifest->generatedOutputPath,
                    error))
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "ASSET_IMPORT_GENERATE_FAILED",
             .message = error,
             .path = manifest->generatedOutputPath->string()});
    }
    if (!writeJson(manifestPath, document))
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_IMPORT_WRITE_FAILED",
                             .message = "Could not update asset manifest.",
                             .path = manifestPath.string()});
  } catch (const nlohmann::json::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_MANIFEST_INVALID",
                           .message = error.what(),
                           .path = manifestPath.string()});
  }
  return diagnostics;
}

} // namespace demi::assets
