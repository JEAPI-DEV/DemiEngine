#include "demi/assets/TerrainAssetCook.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainAssetPayload.h"
#include "demi/assets/TerrainAssetStorage.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace demi::assets {
namespace {

void report(TerrainAssetCookResult &result, const AssetManifest &manifest,
            const std::exception &error) {
  result.diagnostics.push_back({.severity = Severity::Error,
                                .code = "TERRAIN_ASSET_COOK_FAILED",
                                .message = error.what(),
                                .path = manifest.sourcePath.string()});
}

std::filesystem::path
binaryRelativePath(const std::filesystem::path &relativeSource) {
  auto binary = relativeSource;
  binary.replace_extension(".bin");
  return binary;
}

void requireCurrentSource(const AssetManifest &manifest) {
  const auto current = hashFiles(manifest.sourcePaths);
  if (!current || *current != manifest.sourceHash)
    throw std::runtime_error(
        "Terrain source changed while cooking; reimport and retry.");
}

void requireSafeOutput(const std::filesystem::path &root,
                       const std::filesystem::path &path) {
  if (!pathIsInside(root, path))
    throw std::runtime_error("Terrain cook output escapes the cook directory.");
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory)
    return;
  if (error || std::filesystem::is_symlink(status) ||
      (std::filesystem::exists(status) &&
       !std::filesystem::is_regular_file(status)))
    throw std::runtime_error("Terrain cook target is not a regular file: " +
                             path.string());
}

std::filesystem::path
makeStageDirectory(const std::filesystem::path &outputDirectory) {
  static std::atomic_uint64_t sequence{0};
  const auto cacheRoot = outputDirectory / ".cook-cache";
  std::error_code error;
  const auto cacheStatus = std::filesystem::symlink_status(cacheRoot, error);
  if (error == std::errc::no_such_file_or_directory)
    error.clear();
  if (error || std::filesystem::is_symlink(cacheStatus) ||
      (std::filesystem::exists(cacheStatus) &&
       !std::filesystem::is_directory(cacheStatus)))
    throw std::runtime_error("Terrain cook cache path is unsafe.");
  const auto parent = cacheRoot / "terrain-staging";
  std::filesystem::create_directories(parent, error);
  if (error || !pathIsInside(outputDirectory, parent))
    throw std::runtime_error("Cannot create terrain cook staging directory.");
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto stage = parent / (std::to_string(nonce) + "-" +
                               std::to_string(sequence.fetch_add(1)));
  if (!std::filesystem::create_directory(stage, error) || error)
    throw std::runtime_error("Cannot reserve terrain cook staging directory.");
  return stage;
}

void moveFile(const std::filesystem::path &from,
              const std::filesystem::path &to) {
  std::error_code error;
  std::filesystem::rename(from, to, error);
  if (error)
    throw std::runtime_error("Cannot publish cooked terrain file: " +
                             error.message());
}

void removeStageFile(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory)
    return;
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status))
    throw std::runtime_error("Terrain staging file is unsafe: " +
                             path.string());
  std::filesystem::remove(path, error);
  if (error)
    throw std::runtime_error("Cannot clean terrain staging file: " +
                             error.message());
}

void cleanStage(const std::filesystem::path &stage,
                const std::filesystem::path &binary,
                const std::filesystem::path &manifest) {
  removeStageFile(binary);
  removeStageFile(manifest);
  removeStageFile(stage / "previous.binary");
  removeStageFile(stage / "previous.manifest");
  std::error_code error;
  std::filesystem::remove(stage, error);
  if (error)
    throw std::runtime_error("Cannot clean terrain staging directory: " +
                             error.message());
}

void publishPair(const std::filesystem::path &stage,
                 const std::filesystem::path &stagedBinary,
                 const std::filesystem::path &stagedManifest,
                 const std::filesystem::path &targetBinary,
                 const std::filesystem::path &targetManifest,
                 bool &rollbackFailed) {
  const auto priorBinary = stage / "previous.binary";
  const auto priorManifest = stage / "previous.manifest";
  bool savedBinary = false;
  bool savedManifest = false;
  bool publishedBinary = false;
  bool publishedManifest = false;
  try {
    if (std::filesystem::exists(targetBinary)) {
      moveFile(targetBinary, priorBinary);
      savedBinary = true;
    }
    if (std::filesystem::exists(targetManifest)) {
      moveFile(targetManifest, priorManifest);
      savedManifest = true;
    }
    moveFile(stagedBinary, targetBinary);
    publishedBinary = true;
    moveFile(stagedManifest, targetManifest);
    publishedManifest = true;
  } catch (const std::exception &original) {
    try {
      if (publishedManifest)
        removeStageFile(targetManifest);
      if (publishedBinary)
        removeStageFile(targetBinary);
      if (savedManifest)
        moveFile(priorManifest, targetManifest);
      if (savedBinary)
        moveFile(priorBinary, targetBinary);
    } catch (const std::exception &restore) {
      rollbackFailed = true;
      throw std::runtime_error(std::string(original.what()) +
                               "; rollback failed: " + restore.what() +
                               ". Previous files remain in " + stage.string());
    }
    throw;
  }
}

std::map<std::string, std::string>
priorCookHashes(const std::filesystem::path &manifestPath) {
  std::map<std::string, std::string> hashes;
  if (!std::filesystem::exists(manifestPath))
    return hashes;
  std::ifstream input(manifestPath);
  if (!input)
    throw std::runtime_error("Cannot read the previous cook manifest.");
  const auto document = nlohmann::json::parse(input);
  if (!document.is_object() || !document.contains("files") ||
      !document.at("files").is_array())
    throw std::runtime_error("Previous cook manifest has no files array.");
  for (const auto &entry : document.at("files")) {
    if (!entry.is_object() || !entry.contains("path") ||
        !entry.at("path").is_string() || !entry.contains("hash") ||
        !entry.at("hash").is_string())
      throw std::runtime_error(
          "Previous cook manifest has an invalid file entry.");
    hashes.emplace(entry.at("path").get<std::string>(),
                   entry.at("hash").get<std::string>());
  }
  return hashes;
}

std::vector<std::filesystem::path>
staleTerrainSources(const std::filesystem::path &outputDirectory,
                    const std::map<std::string, std::string> &previousHashes) {
  std::vector<std::filesystem::path> sources;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator
           iterator(outputDirectory, error),
       end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_directory(error) &&
        iterator->path().filename() == ".cook-cache") {
      iterator.disable_recursion_pending();
      continue;
    }
    const auto &path = iterator->path();
    if (!path.filename().string().ends_with(".terrain.json"))
      continue;
    if (error || iterator->is_symlink(error) ||
        !iterator->is_regular_file(error) ||
        !pathIsInside(outputDirectory, path))
      throw std::runtime_error("Cook output has an unsafe terrain source: " +
                               path.string());
    const std::string relative =
        std::filesystem::relative(path, outputDirectory).generic_string();
    const auto prior = previousHashes.find(relative);
    const auto actual = hashFile(path);
    if (prior == previousHashes.end() || !actual || *actual != prior->second)
      throw std::runtime_error(
          "Cook output contains an untracked or changed terrain source: " +
          path.string());
    sources.push_back(path);
  }
  if (error)
    throw std::runtime_error("Cannot scan cooked terrain sources: " +
                             error.message());
  return sources;
}

} // namespace

nlohmann::json terrainAssetCookSettings(const AssetManifest &manifest,
                                        const AssetRegistry &registry) {
  const TerrainAssetSource source = loadTerrainAssetSource(manifest);
  const runtime::TerrainRecipe recipe =
      runtime::TerrainRecipe::parse(source.recipe);
  const runtime::TerrainGenerationInputs inputs =
      runtime::resolveTerrainGenerationInputs(recipe, registry);
  return {{"terrain_generator_version", runtime::terrainGeneratorVersion},
          {"terrain_cook_version", runtime::terrainCookVersion},
          {"terrain_payload_version", terrain_payload::Version},
          {"terrain_recipe_digest", runtime::terrainCookRecipeDigest(recipe)},
          {"terrain_input_fingerprint",
           terrain_storage::assetInputFingerprint(registry, recipe, inputs)}};
}

TerrainAssetCookResult
cookRegisteredTerrainAsset(const AssetManifest &manifest,
                           const AssetRegistry &registry,
                           const std::filesystem::path &outputDirectory,
                           const std::filesystem::path &relativeManifest,
                           const std::filesystem::path &relativeSource) {
  TerrainAssetCookResult result;
  std::filesystem::path stage;
  std::filesystem::path stagedBinary;
  std::filesystem::path stagedManifest;
  bool rollbackFailed = false;
  try {
    requireCurrentSource(manifest);
    const std::filesystem::path prepared =
        prepareTerrainAsset(registry, manifest.id);
    if (!std::filesystem::is_regular_file(prepared))
      throw std::runtime_error(
          "Terrain preparation produced no cooked payload.");

    const std::filesystem::path relativeBinary =
        binaryRelativePath(relativeSource);
    const std::filesystem::path targetBinary = outputDirectory / relativeBinary;
    const std::filesystem::path targetManifest =
        outputDirectory / relativeManifest;
    requireSafeOutput(outputDirectory, targetBinary);
    requireSafeOutput(outputDirectory, targetManifest);
    stage = makeStageDirectory(outputDirectory);
    stagedBinary = stage / relativeBinary.filename();
    stagedManifest = stage / relativeManifest.filename();
    std::error_code error;
    std::filesystem::copy_file(prepared, stagedBinary, error);
    if (error)
      throw std::runtime_error("Could not stage cooked terrain: " +
                               error.message());
    const auto binaryHash = hashFiles({stagedBinary});
    if (!binaryHash)
      throw std::runtime_error("Could not hash cooked terrain payload.");
    std::ifstream input(manifest.manifestPath);
    if (!input)
      throw std::runtime_error("Could not read terrain asset manifest.");
    nlohmann::json cookedManifest = nlohmann::json::parse(input);
    cookedManifest["source"] =
        relativeBinary.lexically_relative(relativeManifest.parent_path())
            .generic_string();
    cookedManifest["source_hash"] = *binaryHash;
    cookedManifest.erase("generated_output");
    std::ofstream output(stagedManifest);
    if (!output)
      throw std::runtime_error(
          "Could not write cooked terrain asset manifest.");
    output << cookedManifest.dump(2) << '\n';
    if (!output)
      throw std::runtime_error("Could not complete cooked terrain manifest.");
    output.close();

    const auto staged = terrain_storage::read(stagedBinary);
    const auto expected = terrainAssetCookSettings(manifest, registry);
    if (staged.recipeDigest !=
            expected.at("terrain_recipe_digest").get<std::string>() ||
        staged.inputFingerprint !=
            expected.at("terrain_input_fingerprint").get<std::string>())
      throw std::runtime_error("Staged terrain payload has stale provenance.");
    const auto declaredBinary = (targetManifest.parent_path() /
                                 cookedManifest.at("source").get<std::string>())
                                    .lexically_normal();
    if (declaredBinary != targetBinary.lexically_normal() ||
        cookedManifest.at("source_hash").get<std::string>() != *binaryHash ||
        hashFiles({stagedBinary}) != binaryHash)
      throw std::runtime_error(
          "Staged terrain manifest does not match its payload.");
    requireCurrentSource(manifest);
    std::filesystem::create_directories(targetBinary.parent_path(), error);
    if (error)
      throw std::runtime_error("Could not create terrain output directory: " +
                               error.message());
    requireSafeOutput(outputDirectory, targetBinary);
    requireSafeOutput(outputDirectory, targetManifest);
    publishPair(stage, stagedBinary, stagedManifest, targetBinary,
                targetManifest, rollbackFailed);

    result.outputs = {targetBinary, targetManifest};
  } catch (const std::exception &error) {
    report(result, manifest, error);
  }
  if (!stage.empty() && !rollbackFailed) {
    try {
      cleanStage(stage, stagedBinary, stagedManifest);
    } catch (const std::exception &error) {
      result.diagnostics.push_back(
          {.severity = Severity::Warning,
           .code = "TERRAIN_ASSET_COOK_CLEANUP_DEFERRED",
           .message = error.what(),
           .path = stage.string()});
    }
  }
  return result;
}

Diagnostics publishCookManifestWithTerrainPrune(
    const std::filesystem::path &outputDirectory,
    const nlohmann::json &manifest) {
  Diagnostics diagnostics;
  const auto targetManifest = outputDirectory / "cook.manifest.json";
  std::filesystem::path stage;
  std::filesystem::path stagedManifest;
  std::filesystem::path priorManifest;
  std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
  bool savedManifest = false;
  bool publishedManifest = false;
  bool rollbackFailed = false;
  try {
    requireSafeOutput(outputDirectory, targetManifest);
    const auto previousHashes = priorCookHashes(targetManifest);
    const auto staleSources =
        staleTerrainSources(outputDirectory, previousHashes);

    stage = makeStageDirectory(outputDirectory);
    stagedManifest = stage / "next.cook.manifest.json";
    priorManifest = stage / "previous.cook.manifest.json";
    {
      std::ofstream output(stagedManifest);
      if (!output)
        throw std::runtime_error("Cannot stage the new cook manifest.");
      output << manifest.dump(2) << '\n';
      if (!output)
        throw std::runtime_error("Cannot complete the new cook manifest.");
    }
    for (std::size_t index = 0; index < staleSources.size(); ++index) {
      const auto backup = stage / (std::to_string(index) + ".terrain.json");
      requireSafeOutput(outputDirectory, staleSources[index]);
      moveFile(staleSources[index], backup);
      moved.emplace_back(staleSources[index], backup);
    }
    if (std::filesystem::exists(targetManifest)) {
      moveFile(targetManifest, priorManifest);
      savedManifest = true;
    }
    moveFile(stagedManifest, targetManifest);
    publishedManifest = true;
  } catch (const std::exception &error) {
    const std::string original = error.what();
    std::string restoreFailure;
    const auto restore = [&](const auto &operation) {
      try {
        operation();
      } catch (const std::exception &failure) {
        if (!restoreFailure.empty())
          restoreFailure += "; ";
        restoreFailure += failure.what();
      }
    };
    if (publishedManifest)
      restore([&] { removeStageFile(targetManifest); });
    if (savedManifest)
      restore([&] { moveFile(priorManifest, targetManifest); });
    for (auto iterator = moved.rbegin(); iterator != moved.rend(); ++iterator)
      restore([&] { moveFile(iterator->second, iterator->first); });
    rollbackFailed = !restoreFailure.empty();
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = rollbackFailed ? "COOK_TERRAIN_PRUNE_ROLLBACK_FAILED"
                                : "COOK_TERRAIN_PRUNE_FAILED",
         .message = rollbackFailed
                        ? original + "; rollback failed: " + restoreFailure +
                              "; backup: " + stage.string()
                        : original,
         .path = outputDirectory.string()});
  }

  if (!stage.empty() && !rollbackFailed) {
    try {
      removeStageFile(stagedManifest);
      removeStageFile(priorManifest);
      for (const auto &[source, backup] : moved) {
        (void)source;
        removeStageFile(backup);
      }
      std::error_code error;
      std::filesystem::remove(stage, error);
      if (error)
        throw std::runtime_error(
            "Cannot remove empty cook staging directory: " + error.message());
    } catch (const std::exception &error) {
      diagnostics.push_back({.severity = Severity::Warning,
                             .code = "COOK_TERRAIN_PRUNE_CLEANUP_DEFERRED",
                             .message = error.what(),
                             .path = stage.string()});
    }
  }
  return diagnostics;
}

} // namespace demi::assets
