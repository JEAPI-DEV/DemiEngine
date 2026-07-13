#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetPackage.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace demi;

namespace {

void writeText(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

void writeJson(const std::filesystem::path &path,
               const nlohmann::json &document) {
  std::ofstream output(path);
  output << document.dump(2) << '\n';
}

bool containsCode(const Diagnostics &diagnostics, const std::string &code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic &diagnostic) {
    return diagnostic.code == code;
  });
}

void createProject(const std::filesystem::path &directory) {
  writeText(directory / "demi.project.json",
            R"({"format_version":1,"name":"Asset Fixture","scenes":[]})");
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_asset_pipeline_" + std::to_string(nonce));
  const auto sourceProject = root / "source";
  const auto targetProject = root / "target";
  const auto external = root / "external";
  createProject(sourceProject);
  createProject(targetProject);
  writeText(external / "main.ppm", "P3\n1 1\n255\n255 0 0\n");
  writeText(external / "dependency.png", "deterministic-png-fixture");

  const auto dependency =
      assets::importAsset({.projectDirectory = sourceProject,
                           .source = external / "dependency.png",
                           .id = "asset://fixture/dependency"});
  const auto mainAsset = assets::importAsset({.projectDirectory = sourceProject,
                                              .source = external / "main.ppm",
                                              .id = "asset://fixture/main"});
  if (hasErrors(dependency.diagnostics) || hasErrors(mainAsset.diagnostics)) {
    std::cerr << "Asset import failed.\n";
    return 1;
  }

  auto mainDocument = readJson(mainAsset.manifestPath);
  mainDocument["dependencies"] = {"asset://fixture/dependency"};
  mainDocument["attribution"] = "DemiEngine test fixture";
  mainDocument["settings"] = {
      {"filter", "nearest"}, {"wrap", "clamp"}, {"mipmaps", true}};
  writeJson(mainAsset.manifestPath, mainDocument);
  const auto manifest = loadAssetManifest(mainAsset.manifestPath);
  if (!manifest || manifest->importer != "image" ||
      manifest->importerVersion != 1 || manifest->sourceHash.empty() ||
      manifest->dependencies.size() != 1 ||
      manifest->textureSettings.filter != "nearest" ||
      manifest->textureSettings.wrap != "clamp" ||
      !manifest->textureSettings.mipmaps || !manifest->generatedOutputPath ||
      !std::filesystem::exists(*manifest->generatedOutputPath)) {
    std::cerr << "Expanded manifest metadata was not preserved.\n";
    return 1;
  }

  mainDocument["settings"]["filter"] = "invalid";
  writeJson(mainAsset.manifestPath, mainDocument);
  if (!containsCode(validateAssetRegistry(loadAssetRegistry(sourceProject)),
                    "ASSET_TEXTURE_FILTER_INVALID")) {
    std::cerr << "Invalid texture importer setting was not diagnosed.\n";
    return 1;
  }
  mainDocument["settings"]["filter"] = "nearest";
  writeJson(mainAsset.manifestPath, mainDocument);

  writeText(external / "duplicate.png", "duplicate-id-fixture");
  const auto duplicate =
      assets::importAsset({.projectDirectory = sourceProject,
                           .source = external / "duplicate.png",
                           .id = "asset://fixture/dependency"});
  if (!containsCode(duplicate.diagnostics, "ASSET_IMPORT_CONFLICT")) {
    std::cerr << "Duplicate stable asset ID was not rejected.\n";
    return 1;
  }

  auto dependencyDocument = readJson(dependency.manifestPath);
  dependencyDocument["dependencies"] = {"asset://fixture/main"};
  writeJson(dependency.manifestPath, dependencyDocument);
  if (!containsCode(validateAssetRegistry(loadAssetRegistry(sourceProject)),
                    "ASSET_DEPENDENCY_CYCLE")) {
    std::cerr << "Asset dependency cycle was not diagnosed.\n";
    return 1;
  }
  dependencyDocument["dependencies"] = nlohmann::json::array();
  writeJson(dependency.manifestPath, dependencyDocument);

  mainDocument["dependencies"] = {"asset://fixture/missing"};
  writeJson(mainAsset.manifestPath, mainDocument);
  if (!containsCode(validateAssetRegistry(loadAssetRegistry(sourceProject)),
                    "ASSET_DEPENDENCY_NOT_FOUND")) {
    std::cerr << "Missing asset dependency was not diagnosed.\n";
    return 1;
  }
  mainDocument["dependencies"] = {"asset://fixture/dependency"};
  writeJson(mainAsset.manifestPath, mainDocument);

  const auto packageA = root / "fixture-a.demipack";
  const auto packageB = root / "fixture-b.demipack";
  const assets::AssetPackageExportRequest exportRequest{
      .projectDirectory = sourceProject,
      .outputPath = packageA,
      .assetIds = {"asset://fixture/main"}};
  if (hasErrors(assets::exportAssetPackage(exportRequest))) {
    std::cerr << "Asset package export failed.\n";
    return 1;
  }
  auto secondExport = exportRequest;
  secondExport.outputPath = packageB;
  if (hasErrors(assets::exportAssetPackage(secondExport)) ||
      assets::hashFile(packageA) != assets::hashFile(packageB)) {
    std::cerr << "Asset package output was not deterministic.\n";
    return 1;
  }

  const auto corruptPackage = root / "fixture-corrupt.demipack";
  std::filesystem::copy_file(packageA, corruptPackage);
  {
    std::fstream package(corruptPackage,
                         std::ios::binary | std::ios::in | std::ios::out);
    package.seekg(-1, std::ios::end);
    const int lastByte = package.get();
    package.seekp(-1, std::ios::end);
    package.put(static_cast<char>(lastByte ^ 0xff));
  }
  if (!containsCode(
          assets::importAssetPackage({.projectDirectory = targetProject,
                                      .packagePath = corruptPackage}),
          "ASSET_PACKAGE_CORRUPT")) {
    std::cerr << "Corrupted asset package was not rejected.\n";
    return 1;
  }

  std::vector<std::string> conflicts;
  if (hasErrors(assets::importAssetPackage(
          {.projectDirectory = targetProject, .packagePath = packageA},
          &conflicts)) ||
      findAsset(loadAssetRegistry(targetProject),
                "asset://fixture/dependency") == nullptr ||
      hasErrors(validateAssetRegistry(loadAssetRegistry(targetProject)))) {
    std::cerr << "Asset package dependency round trip failed.\n";
    return 1;
  }
  const auto conflictDiagnostics = assets::importAssetPackage(
      {.projectDirectory = targetProject, .packagePath = packageA}, &conflicts);
  if (!containsCode(conflictDiagnostics, "ASSET_PACKAGE_CONFLICT") ||
      conflicts.empty()) {
    std::cerr << "Asset package conflicts were not reported.\n";
    return 1;
  }

  writeText(manifest->sourcePath, "changed");
  if (!containsCode(validateAssetRegistry(loadAssetRegistry(sourceProject)),
                    "ASSET_SOURCE_STALE") ||
      hasErrors(assets::reimportAsset(mainAsset.manifestPath)) ||
      hasErrors(validateAssetRegistry(loadAssetRegistry(sourceProject)))) {
    std::cerr << "Stale source diagnostics or reimport failed.\n";
    return 1;
  }

  const auto cooked = root / "cooked";
  if (hasErrors(assets::cookProject(
          {.projectFile = sourceProject / "demi.project.json",
           .outputDirectory = cooked,
           .platform = "linux"})) ||
      !std::filesystem::exists(cooked / "cook.manifest.json") ||
      !std::filesystem::exists(cooked / "assets/fixture/main/main.ppm") ||
      !std::filesystem::exists(cooked /
                               "assets/fixture/dependency/dependency.png")) {
    std::cerr << "Project cooking failed.\n";
    return 1;
  }

  if (!assets::importerFor("image.png") || !assets::importerFor("sound.ogg") ||
      !assets::importerFor("model.glb") || assets::importerFor("unknown.xyz")) {
    std::cerr << "Production importer catalog is incomplete.\n";
    return 1;
  }
  if (!containsCode(assets::cookProject(
                        {.projectFile = sourceProject / "demi.project.json",
                         .outputDirectory = root / "android-cooked",
                         .platform = "android"}),
                    "COOK_PLATFORM_UNSUPPORTED")) {
    std::cerr << "Unsupported cook platform was not diagnosed.\n";
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
