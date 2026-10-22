#include "demi/assets/ModelLodGenerator.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/GltfDocument.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/ModelLodPublication.h"
#include "demi/assets/ModelLodSimplifier.h"
#include "demi/filesystem/AuthoredJsonPatch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace demi::assets {
namespace {
using Json = nlohmann::json;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readText(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Could not read " + path.string());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  require(bool(input), "Could not read " + path.string());
  return Json::parse(input);
}

void writeBytes(const std::filesystem::path &path,
                std::span<const std::byte> bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  require(bool(output), "Could not prepare " + path.string());
}

void writeJson(const std::filesystem::path &path, const Json &value) {
  const auto text = value.dump(2) + "\n";
  writeBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}

std::vector<std::filesystem::path> sourcePayloads(const AssetManifest &manifest,
                                                  const cgltf_data &document) {
  std::set<std::filesystem::path> files{manifest.sourcePath};
  const auto addUri = [&](const char *uri) {
    if (!uri || std::string_view(uri).starts_with("data:")) {
      return;
    }
    std::string decoded(uri);
    decoded.resize(cgltf_decode_uri(decoded.data()));
    const auto path = manifest.sourcePath.parent_path() / decoded;
    require(std::filesystem::is_regular_file(path),
            "Model LOD input is not a regular source file: " + path.string());
    files.insert(path);
  };
  for (std::size_t index = 0; index < document.buffers_count; ++index) {
    addUri(document.buffers[index].uri);
  }
  for (std::size_t index = 0; index < document.images_count; ++index) {
    addUri(document.images[index].uri);
  }
  if (manifest.texturePath) {
    files.insert(*manifest.texturePath);
  }
  if (manifest.licensePath) {
    files.insert(*manifest.licensePath);
  }
  return {files.begin(), files.end()};
}

bool validParameters(float medium, float low, float error) {
  return std::isfinite(medium) && std::isfinite(low) && std::isfinite(error) &&
         low > 0 && low <= medium && medium < 1 && error >= 0 && error <= 1;
}

void validateStaticSource(const cgltf_data &document) {
  require(document.skins_count == 0,
          "Automatic model LOD generation does not support skinned models. "
          "Create animated LODs in the source art tool.");
  require(document.animations_count == 0,
          "Automatic model LOD generation does not support animated nodes. "
          "Create animated LODs in the source art tool.");
  for (std::size_t mesh = 0; mesh < document.meshes_count; ++mesh) {
    require(document.meshes[mesh].weights_count == 0,
            "Automatic model LOD generation does not support morph weights.");
    for (std::size_t primitive = 0;
         primitive < document.meshes[mesh].primitives_count; ++primitive) {
      const auto &value = document.meshes[mesh].primitives[primitive];
      require(value.targets_count == 0,
              "Automatic model LOD generation does not support morph targets.");
      require(!value.has_draco_mesh_compression,
              "Decode Draco-compressed geometry before generating static model "
              "LODs.");
      for (std::size_t attribute = 0; attribute < value.attributes_count;
           ++attribute) {
        const auto type = value.attributes[attribute].type;
        require(type != cgltf_attribute_type_joints &&
                    type != cgltf_attribute_type_weights,
                "Automatic model LOD generation does not support skin vertex "
                "attributes.");
      }
    }
  }
  for (std::size_t view = 0; view < document.buffer_views_count; ++view) {
    require(
        !document.buffer_views[view].has_meshopt_compression,
        "Decode compressed mesh buffers before generating static model LODs.");
  }
}

Json recipeFor(const ModelLodGenerationRequest &request) {
  return {{"format_version", 1},
          {"medium_ratio", request.mediumRatio},
          {"low_ratio", request.lowRatio},
          {"max_error", request.maxError}};
}

void verifyOwnedOutput(const std::filesystem::path &directory,
                       const std::string &sourceId) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  require(!std::filesystem::is_symlink(
              std::filesystem::symlink_status(directory)) &&
              std::filesystem::is_directory(directory),
          "Model LOD output destination is not an owned directory.");
  const auto previous = readJson(directory / "generation.lods.json");
  require(previous.value("format_version", 0) == 1 &&
              previous.value("source_model", "") == sourceId,
          "Model LOD output directory belongs to another source.");
  const auto files = previous.at("files").get<std::set<std::string>>();
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    require(entry.is_regular_file() && !entry.is_symlink() &&
                files.contains(entry.path().filename().string()),
            "Model LOD output contains an unowned file: " +
                entry.path().string());
  }
}

bool samePreparedContent(const ModelLodReplacement &replacement) {
  if (std::filesystem::is_symlink(
          std::filesystem::symlink_status(replacement.destination))) {
    return false;
  }
  if (std::filesystem::is_regular_file(replacement.staged)) {
    return std::filesystem::is_regular_file(replacement.destination) &&
           hashFile(replacement.staged) == hashFile(replacement.destination);
  }
  if (!std::filesystem::is_directory(replacement.destination)) {
    return false;
  }
  std::size_t prepared = 0;
  for (const auto &entry :
       std::filesystem::directory_iterator(replacement.staged)) {
    ++prepared;
    const auto target = replacement.destination / entry.path().filename();
    if (!entry.is_regular_file() ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(target)) ||
        !std::filesystem::is_regular_file(target) ||
        hashFile(entry.path()) != hashFile(target)) {
      return false;
    }
  }
  return prepared ==
         std::size_t(std::distance(
             std::filesystem::directory_iterator(replacement.destination),
             std::filesystem::directory_iterator{}));
}

bool variantsCurrent(const AssetManifest &source, const AssetRegistry &registry,
                     const Json &recipe) {
  try {
    std::string error;
    const auto document = gltf::parseDocument(source.sourcePath, error);
    if (!document || hashFiles(source.sourcePaths) != source.sourceHash) {
      return false;
    }
    const auto payload = hashFiles(sourcePayloads(source, *document));
    if (!payload) {
      return false;
    }
    auto expectedSettings = Json::parse(source.settingsJson);
    expectedSettings.erase("model_lods");
    expectedSettings.erase("animations");
    for (const std::string level : {"medium", "low"}) {
      const auto *variant = findAsset(registry, source.id + "/lod/" + level);
      if (!variant || variant->type != "Model3D" ||
          hashFiles(variant->sourcePaths) != variant->sourceHash) {
        return false;
      }
      auto settings = Json::parse(variant->settingsJson);
      const auto provenance = settings.at("model_lod");
      settings.erase("model_lod");
      if (settings != expectedSettings ||
          provenance.value("generator_version", 0) != 1 ||
          provenance.at("source_model") != source.id ||
          provenance.at("source_hash") != *payload ||
          provenance.at("level") != level ||
          provenance.at("triangle_ratio") != recipe.at(level + "_ratio") ||
          provenance.at("max_error") != recipe.at("max_error") ||
          variant->dependencies != source.dependencies ||
          variant->attribution != source.attribution) {
        return false;
      }
      for (const auto &[original, generated] :
           std::array{std::pair{source.texturePath, variant->texturePath},
                      std::pair{source.licensePath, variant->licensePath}}) {
        if (original.has_value() != generated.has_value() ||
            (original && hashFile(*original) != hashFile(*generated))) {
          return false;
        }
      }
    }
    if (source.generatedOutputPath &&
        (!std::filesystem::is_regular_file(*source.generatedOutputPath) ||
         hashFile(source.sourcePath) !=
             hashFile(*source.generatedOutputPath))) {
      return false;
    }
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

struct Workspace {
  std::filesystem::path path;
  bool preserve = false;
  ~Workspace() {
    if (!preserve && !path.empty()) {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  }
};

std::filesystem::path makeWorkspace(const std::filesystem::path &project) {
  static std::atomic<std::uint64_t> sequence{0};
  const auto root = project / ".demi/model-lod-generation";
  require(pathIsInside(project, root),
          "Model LOD workspace escapes the project through a symlink.");
  std::filesystem::create_directories(root);
  for (;;) {
    const auto name =
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(sequence.fetch_add(1));
    const auto path = root / name;
    if (std::filesystem::create_directory(path)) {
      return path;
    }
  }
}
} // namespace

Diagnostics validateModelLodSettings(const Json &settings,
                                     const std::filesystem::path &path) {
  Diagnostics diagnostics;
  if (!settings.is_object() || !settings.contains("model_lods")) {
    return diagnostics;
  }
  try {
    const auto &recipe = settings.at("model_lods");
    require(recipe.is_object() && recipe.at("format_version") == 1 &&
                validParameters(recipe.at("medium_ratio").get<float>(),
                                recipe.at("low_ratio").get<float>(),
                                recipe.at("max_error").get<float>()),
            "Model LOD recipe requires format_version 1, 0 < low_ratio <= "
            "medium_ratio < 1, and 0 <= max_error <= 1.");
  } catch (const std::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "MODEL_LOD_RECIPE_INVALID",
                           .message = error.what(),
                           .path = path.string()});
  }
  return diagnostics;
}

ModelLodGenerationResult
generateModelLods(const ModelLodGenerationRequest &request) {
  ModelLodGenerationResult result;
  Workspace workspace;
  try {
    require(validParameters(request.mediumRatio, request.lowRatio,
                            request.maxError),
            "Model LOD generation requires 0 < low ratio <= medium ratio < 1 "
            "and an error bound from 0 to 1.");
    const auto project =
        std::filesystem::weakly_canonical(request.projectDirectory);
    const auto manifestPath =
        std::filesystem::weakly_canonical(request.modelManifestPath);
    require(std::filesystem::is_regular_file(project / "demi.project.json"),
            "Model LOD generation requires a project directory.");
    require(
        pathIsInside(project, project / "assets") &&
            pathIsInside(project / "assets", manifestPath),
        "Generate LODs from a project-owned model under assets. Import package "
        "art into the project before changing its generation recipe.");
    Diagnostic diagnostic;
    const auto manifest = loadAssetManifest(manifestPath, &diagnostic);
    require(manifest.has_value(), manifest ? "" : diagnostic.message);
    require(manifest->type == "Model3D" &&
                (manifest->sourcePath.extension() == ".gltf" ||
                 manifest->sourcePath.extension() == ".glb"),
            "Automatic LOD generation requires a glTF or GLB Model3D asset.");
    require(manifest->id.starts_with("asset://") && manifest->id.size() > 8,
            "The source model requires a stable asset:// ID.");
    result.sourceModelId = manifest->id;
    result.recipePath = manifestPath;
    const auto importer = importerFor(manifest->sourcePath, "Model3D");
    require(importer.has_value(), "No model importer is available.");
    const auto manifestSnapshot = hashFile(manifestPath);
    require(manifestSnapshot.has_value(),
            "Could not snapshot the source model manifest.");
    const auto originalText = readText(manifestPath);
    auto sourceDocument = Json::parse(originalText);
    const auto originalDocument = sourceDocument;
    auto settings = sourceDocument.value("settings", Json::object());
    require(settings.is_object() && !settings.contains("model_lod"),
            "Regenerate LODs from the original source model, not a generated "
            "LOD variant.");
    Diagnostics profileDiagnostics;
    require(parseModelImportProfile(settings, &profileDiagnostics,
                                    manifestPath.string())
                .has_value(),
            "The source model import profile is invalid.");
    std::string error;
    const auto document = gltf::parseDocument(manifest->sourcePath, error);
    require(bool(document), error);
    validateStaticSource(*document);
    const auto payloadFiles = sourcePayloads(*manifest, *document);
    for (const auto &path : payloadFiles) {
      require(
          pathIsInside(project, path) && std::filesystem::is_regular_file(path),
          "Model LOD sources must be regular files inside the source project.");
    }
    const auto payloadSnapshot = hashFiles(payloadFiles);
    require(payloadSnapshot.has_value(),
            "Could not snapshot source model payloads.");
    require(gltf::loadDocumentBuffers(*document, manifest->sourcePath, error),
            error);

    const auto idBytes =
        std::as_bytes(std::span(manifest->id.data(), manifest->id.size()));
    const auto idHash =
        hashBytes({reinterpret_cast<const unsigned char *>(idBytes.data()),
                   idBytes.size()});
    const auto outputDirectory =
        project / "assets/generated/model_lods" / idHash.substr(8);
    require(pathIsInside(project, outputDirectory),
            "Model LOD output escapes the project through a symlink.");
    require(!pathIsInside(outputDirectory, manifestPath) &&
                !pathIsInside(outputDirectory, manifest->sourcePath),
            "The model source cannot be inside its generated LOD output "
            "directory.");
    verifyOwnedOutput(outputDirectory, manifest->id);
    const auto registry = loadAuthoredAssetRegistry(project);
    const std::array<std::string, 2> levels{"medium", "low"};
    for (const auto &level : levels) {
      const auto *existing =
          findAsset(registry, manifest->id + "/lod/" + level);
      require(existing == nullptr ||
                  std::filesystem::weakly_canonical(existing->manifestPath) ==
                      outputDirectory / (level + ".asset.json"),
              "A generated model LOD asset ID already belongs to another "
              "manifest.");
    }
    workspace.path = makeWorkspace(project);
    const auto stagedDirectory = workspace.path / "outputs";
    std::filesystem::create_directories(stagedDirectory);
    std::set<std::string> outputFiles{"generation.lods.json"};
    const auto copyCompanion =
        [&](const std::optional<std::filesystem::path> &path,
            const std::string &stem) -> std::optional<std::string> {
      if (!path) {
        return std::nullopt;
      }
      const auto filename = stem + path->extension().string();
      std::filesystem::copy_file(*path, stagedDirectory / filename);
      outputFiles.insert(filename);
      return filename;
    };
    const auto texture =
        copyCompanion(manifest->texturePath, "albedo-override");
    const auto license = copyCompanion(manifest->licensePath, "LICENSE");
    const auto sourceHash = hashFiles(manifest->sourcePaths);
    require(sourceHash.has_value(), "Could not hash source model geometry.");
    auto variantSettings = settings;
    variantSettings.erase("model_lods");
    variantSettings.erase("animations");
    for (std::size_t index = 0; index < levels.size(); ++index) {
      const auto &level = levels[index];
      const float ratio = index == 0 ? request.mediumRatio : request.lowRatio;
      ModelLodSimplification simplified;
      require(simplifyStaticModelLod(*document, ratio, request.maxError,
                                     simplified, error),
              error);
      std::vector<std::byte> bytes;
      require(gltf::writeModelLodGlb(*document, manifest->sourcePath,
                                     simplified.primitives, bytes, error),
              error);
      const auto sourceName = level + ".glb";
      const auto generatedManifestName = level + ".asset.json";
      writeBytes(stagedDirectory / sourceName, bytes);
      auto generatedSettings = variantSettings;
      generatedSettings["model_lod"] = {
          {"format_version", 1},
          {"generator_version", 1},
          {"source_model", manifest->id},
          {"source_hash", *payloadSnapshot},
          {"level", level},
          {"triangle_ratio", ratio},
          {"max_error", request.maxError},
          {"achieved_error", simplified.achievedError}};
      Json generated{
          {"format_version", 1},
          {"id", manifest->id + "/lod/" + level},
          {"type", "Model3D"},
          {"source", sourceName},
          {"importer", "gltf-model"},
          {"importer_version", importer->version},
          {"source_hash", *hashFiles({stagedDirectory / sourceName})},
          {"dependencies", sourceDocument.value("dependencies", Json::array())},
          {"settings", generatedSettings}};
      if (texture) {
        generated["texture"] = *texture;
      }
      if (license) {
        generated["license"] = *license;
      }
      if (sourceDocument.contains("attribution")) {
        generated["attribution"] = sourceDocument["attribution"];
      }
      writeJson(stagedDirectory / generatedManifestName, generated);
      outputFiles.insert(sourceName);
      outputFiles.insert(generatedManifestName);
      result.variants.push_back(
          {.level = level,
           .id = generated.at("id").get<std::string>(),
           .manifestPath = outputDirectory / generatedManifestName,
           .sourceTriangles = simplified.sourceTriangles,
           .triangles = simplified.triangles,
           .achievedError = simplified.achievedError});
      const auto requestedTriangles =
          std::size_t(std::floor(double(simplified.sourceTriangles) * ratio));
      if (simplified.triangles > requestedTriangles) {
        result.diagnostics.push_back(
            {.severity = Severity::Info,
             .code = "MODEL_LOD_REDUCTION_LIMITED",
             .message = level + " LOD retained " +
                        std::to_string(simplified.triangles) +
                        " triangles to preserve material boundaries, seams and "
                        "the error bound.",
             .path = manifestPath.string()});
      }
    }
    writeJson(stagedDirectory / "generation.lods.json",
              {{"format_version", 1},
               {"source_model", manifest->id},
               {"recipe", recipeFor(request)},
               {"files", outputFiles}});
    auto &authoredRecipe = sourceDocument["settings"]["model_lods"];
    if (!authoredRecipe.is_object()) {
      authoredRecipe = Json::object();
    }
    authoredRecipe.update(recipeFor(request));
    sourceDocument["source_hash"] = *sourceHash;
    sourceDocument["importer"] = importer->name;
    sourceDocument["importer_version"] = importer->version;
    const auto stagedManifest = workspace.path / "source.asset.json";
    const auto patched = filesystem::patchAuthoredJsonSource(
        originalText, originalDocument, sourceDocument);
    require(patched.has_value(),
            "Could not preserve source manifest formatting while adding the "
            "model LOD recipe; previous files are unchanged.");
    writeBytes(stagedManifest,
               std::as_bytes(std::span(patched->data(), patched->size())));
    std::filesystem::permissions(
        stagedManifest, std::filesystem::status(manifestPath).permissions());
    std::vector<ModelLodReplacement> replacements{
        {stagedDirectory, outputDirectory}};
    if (manifest->generatedOutputPath) {
      require(
          pathIsInside(project, *manifest->generatedOutputPath) &&
              pathIsInside(project / "generated",
                           *manifest->generatedOutputPath) &&
              std::filesystem::weakly_canonical(
                  *manifest->generatedOutputPath) !=
                  std::filesystem::weakly_canonical(project / "generated") &&
              (!std::filesystem::exists(*manifest->generatedOutputPath) ||
               std::filesystem::is_regular_file(
                   *manifest->generatedOutputPath)),
          "The source model generated mirror must remain inside "
          "project/generated.");
      const auto mirror = workspace.path / "mirror";
      std::filesystem::copy_file(manifest->sourcePath, mirror);
      replacements.push_back({mirror, *manifest->generatedOutputPath});
    }
    replacements.push_back({stagedManifest, manifestPath});
    require(hashFile(manifestPath) == manifestSnapshot &&
                hashFiles(payloadFiles) == payloadSnapshot,
            "Source model inputs changed during LOD generation; previous "
            "outputs are unchanged.");
    std::erase_if(replacements, samePreparedContent);
    if (!publishModelLods(replacements, workspace.path, error)) {
      workspace.preserve = true;
      throw std::runtime_error(error);
    }
  } catch (const std::exception &error) {
    result.variants.clear();
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "MODEL_LOD_GENERATION_FAILED",
                                  .message = error.what(),
                                  .path = request.modelManifestPath.string()});
  }
  return result;
}

const AssetManifest *modelLodSource(const AssetRegistry &registry,
                                    std::string_view generatedId) {
  for (const std::string_view suffix : {"/lod/medium", "/lod/low"}) {
    if (!generatedId.ends_with(suffix)) {
      continue;
    }
    const auto *source = findAsset(
        registry,
        std::string(generatedId.substr(0, generatedId.size() - suffix.size())));
    if (source) {
      const auto settings = Json::parse(source->settingsJson, nullptr, false);
      if (settings.is_object() && settings.contains("model_lods")) {
        return source;
      }
    }
  }
  return nullptr;
}

Diagnostics ensureModelLods(const std::filesystem::path &projectDirectory) {
  Diagnostics diagnostics;
  try {
    const auto registry = loadAuthoredAssetRegistry(projectDirectory);
    for (const auto &source : registry.assets) {
      if (source.type != "Model3D") {
        continue;
      }
      const auto settings = Json::parse(source.settingsJson, nullptr, false);
      if (!settings.is_object() || !settings.contains("model_lods")) {
        continue;
      }
      const auto validation =
          validateModelLodSettings(settings, source.manifestPath);
      diagnostics.insert(diagnostics.end(), validation.begin(),
                         validation.end());
      if (hasErrors(validation)) {
        continue;
      }
      const auto &recipe = settings.at("model_lods");
      if (variantsCurrent(source, registry, recipe)) {
        continue;
      }
      const auto generated = generateModelLods(
          {.projectDirectory = projectDirectory,
           .modelManifestPath = source.manifestPath,
           .mediumRatio = recipe.at("medium_ratio").get<float>(),
           .lowRatio = recipe.at("low_ratio").get<float>(),
           .maxError = recipe.at("max_error").get<float>()});
      diagnostics.insert(diagnostics.end(), generated.diagnostics.begin(),
                         generated.diagnostics.end());
    }
  } catch (const std::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "MODEL_LOD_PREPARATION_FAILED",
                           .message = error.what(),
                           .path = projectDirectory.string()});
  }
  return diagnostics;
}

} // namespace demi::assets
