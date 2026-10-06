#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/packages/PackageLock.h"
#include "demi/packages/PackageManifest.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "demi/schema/Validation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const std::filesystem::path &path, const nlohmann::json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(bool(output), "Fixture write failed");
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

bool hasCode(const demi::Diagnostics &diagnostics, std::string_view code) {
  for (const auto &item : diagnostics)
    if (item.code == code)
      return true;
  return false;
}

void testPipeline(const std::filesystem::path &root) {
  const auto project = root / "project";
  const auto source = root / "external/island.terrain.json";
  const auto cooked = root / "cooked";
  writeJson(project / "demi.project.json",
            {{"format_version", 1},
             {"name", "Terrain fixture"},
             {"scenes", nlohmann::json::array()}});

  demi::runtime::TerrainRecipe recipe;
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 4;
  writeJson(source, {{"format_version", 1},
                     {"id", "asset://terrain/island"},
                     {"recipe", recipe.toJson()}});

  const auto imported =
      demi::assets::importAsset({.projectDirectory = project,
                                 .source = source,
                                 .id = "asset://terrain/island"});
  require(!demi::hasErrors(imported.diagnostics), "Terrain import failed");
  const auto manifest = demi::loadAssetManifest(imported.manifestPath);
  require(manifest && manifest->type == "Terrain" &&
              manifest->importer == "terrain_heightfield" &&
              !manifest->generatedOutputPath,
          "Terrain did not register as a source asset");
  require(demi::classifySourceFile(source) ==
                  demi::SourceFileKind::TerrainAsset &&
              !demi::hasErrors(demi::validatePath(project).diagnostics),
          "Authored terrain failed project validation");

  const auto diagnostics =
      demi::assets::cookProject({.projectFile = project / "demi.project.json",
                                 .outputDirectory = cooked,
                                 .platform = "linux"});
  require(!demi::hasErrors(diagnostics), "Terrain cook failed");

  const auto cookedRegistry = demi::loadAssetRegistry(cooked);
  const auto *binary = demi::findAsset(cookedRegistry, manifest->id);
  require(binary && binary->sourcePath.extension() == ".bin" &&
              std::filesystem::is_regular_file(binary->sourcePath),
          "Cooked terrain binary is missing");
  require(bool(demi::assets::loadTerrainAsset(cookedRegistry, manifest->id)),
          "Cooked terrain cannot be loaded");
  require(!demi::hasErrors(demi::validatePath(cooked).diagnostics),
          "Cooked terrain project failed validation");
  const auto firstPayloadHash = demi::assets::hashFile(binary->sourcePath);
  require(firstPayloadHash.has_value(), "Could not hash cooked terrain");
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(cooked))
    require(!entry.path().filename().string().ends_with(".terrain.json"),
            "Authored terrain recipe leaked into cooked project");

  auto changed = recipe;
  changed.seed += 1;
  writeJson(manifest->sourcePath, {{"format_version", 1},
                                   {"id", "asset://terrain/island"},
                                   {"recipe", changed.toJson()}});
  require(hasCode(demi::validateAssetRegistry(demi::loadAssetRegistry(project)),
                  "ASSET_SOURCE_STALE"),
          "Changed terrain source did not invalidate its manifest");
  require(!demi::hasErrors(demi::assets::reimportAsset(imported.manifestPath)),
          "Terrain reimport failed");
  require(!demi::hasErrors(demi::assets::cookProject(
              {.projectFile = project / "demi.project.json",
               .outputDirectory = cooked,
               .platform = "linux"})),
          "Terrain recook failed");
  require(!demi::hasErrors(demi::validatePath(cooked).diagnostics),
          "Recooked terrain project failed validation");
  require(demi::assets::hashFile(binary->sourcePath) != firstPayloadHash,
          "Changed terrain recipe reused the previous binary");

  // A prior generic DataAsset cook may have copied an authored recipe. Only
  // entries verified against that cook's manifest may be pruned.
  const auto legacy = cooked / "assets/old/island.terrain.json";
  writeJson(legacy, {{"format_version", 1}, {"id", "asset://old/island"}});
  auto previousCook = readJson(cooked / "cook.manifest.json");
  previousCook["files"].push_back({{"path", "assets/old/island.terrain.json"},
                                   {"hash", *demi::assets::hashFile(legacy)}});
  writeJson(cooked / "cook.manifest.json", previousCook);
  require(!demi::hasErrors(demi::assets::cookProject(
              {.projectFile = project / "demi.project.json",
               .outputDirectory = cooked,
               .platform = "linux"})),
          "Verified legacy terrain source could not be pruned");
  require(!std::filesystem::exists(legacy),
          "Verified authored recipe remained in cooked output");

  const auto untracked = cooked / "assets/unknown.terrain.json";
  writeJson(untracked, {{"format_version", 1}});
  const auto previousManifestHash =
      demi::assets::hashFile(cooked / "cook.manifest.json");
  const auto rejected =
      demi::assets::cookProject({.projectFile = project / "demi.project.json",
                                 .outputDirectory = cooked,
                                 .platform = "linux"});
  require(hasCode(rejected, "COOK_TERRAIN_PRUNE_FAILED") &&
              std::filesystem::exists(untracked) &&
              demi::assets::hashFile(cooked / "cook.manifest.json") ==
                  previousManifestHash,
          "Untracked cooked-file cleanup changed a prior artifact");
}

void testLockedPackageTerrain(const std::filesystem::path &root) {
  const auto project = root / "package-project";
  const auto installed = project / ".demi/packages/fixture.terrain";
  const auto source = installed / "assets/source/landscape.terrain.json";
  const auto authoredManifest = installed / "assets/landscape.asset.json";
  const auto cooked = root / "package-cooked";
  writeJson(project / "demi.project.json",
            {{"format_version", 1},
             {"name", "Package terrain fixture"},
             {"scenes", nlohmann::json::array()}});
  demi::runtime::TerrainRecipe recipe;
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 4;
  writeJson(source, {{"format_version", 1},
                     {"id", "asset://fixture/landscape"},
                     {"recipe", recipe.toJson()}});
  writeJson(authoredManifest,
            {{"format_version", 1},
             {"id", "asset://fixture/landscape"},
             {"type", "Terrain"},
             {"source", "source/landscape.terrain.json"},
             {"importer", "terrain_heightfield"},
             {"importer_version", 1},
             {"source_hash", *demi::assets::hashFiles({source})},
             {"dependencies", nlohmann::json::array()},
             {"settings", nlohmann::json::object()}});
  const nlohmann::json packageDocument{
      {"format_version", 1},
      {"name", "fixture.terrain"},
      {"version", "1.0.0"},
      {"engine", "*"},
      {"dependencies", nlohmann::json::object()},
      {"public_modules", nlohmann::json::array()},
      {"exported_events", nlohmann::json::array()},
      {"files",
       {"assets/source/landscape.terrain.json", "assets/landscape.asset.json"}},
      {"asset_manifests", {"assets/landscape.asset.json"}}};
  writeJson(installed / demi::packages::PackageManifestFilename,
            packageDocument);
  const auto parsed =
      demi::packages::parsePackageManifest(packageDocument, "fixture");
  require(parsed.manifest && !demi::hasErrors(parsed.diagnostics),
          "Package fixture manifest rejected");
  const demi::packages::PackageRelease release{
      .manifest = *parsed.manifest,
      .archiveHash = "sha256:fixture",
      .archiveUri = "file:///offline/fixture-terrain.demi-package"};
  writeJson(project / demi::packages::PackageLockFilename,
            demi::packages::packageLockJson({{release.manifest.name, release}},
                                            "offline-test"));
  require(!demi::hasErrors(demi::validatePath(project).diagnostics),
          "Installed Terrain package failed source validation");
  const auto cookedIssues =
      demi::assets::cookProject({.projectFile = project / "demi.project.json",
                                 .outputDirectory = cooked,
                                 .platform = "linux"});
  require(!demi::hasErrors(cookedIssues), "Locked Terrain package cook failed");
  const auto cookedRegistry = demi::loadAssetRegistry(cooked);
  require(!demi::hasErrors(cookedRegistry.diagnostics) &&
              !demi::hasErrors(demi::validatePath(cooked).diagnostics),
          "Cooked Terrain package failed locked-content validation");
  const auto *asset =
      demi::findAsset(cookedRegistry, "asset://fixture/landscape");
  require(asset && asset->sourcePackage == "fixture.terrain" &&
              asset->sourcePath.extension() == ".bin" &&
              bool(demi::assets::loadTerrainAsset(cookedRegistry, asset->id)),
          "Cooked package Terrain asset cannot be loaded");
  require(!std::filesystem::exists(
              cooked /
              "packages/fixture.terrain/assets/source/landscape.terrain.json"),
          "Package shipped an authored Terrain recipe");
  const auto cookedManifest = readJson(cooked / "cook.manifest.json");
  bool hasOrigin = false;
  for (const auto &entry : cookedManifest.at("assets"))
    if (entry.value("asset", "") == asset->id)
      hasOrigin = entry.value("source_package", "") == "fixture.terrain" &&
                  !entry.value("package_content_hash", "").empty();
  require(hasOrigin, "Cooked Terrain lost its locked package provenance");

  std::ofstream corrupt(asset->sourcePath, std::ios::binary | std::ios::app);
  corrupt.put('x');
  corrupt.close();
  require(hasCode(demi::loadAssetRegistry(cooked).diagnostics,
                  "PACKAGE_COOKED_FILE_MISMATCH"),
          "Modified package Terrain binary passed cooked-content verification");
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-terrain-asset-pipeline-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  require(std::filesystem::create_directory(root),
          "Could not create test fixture directory");
  try {
    testPipeline(root);
    testLockedPackageTerrain(root);
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
}
