#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/assets/RuntimeAssetService.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

struct Fixture {
  std::filesystem::path root;
  std::optional<std::string> priorCache;
  Fixture() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "demi-terrain-service-XXXXXX")
            .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *directory = ::mkdtemp(buffer.data());
    require(directory != nullptr, "Cannot create fixture");
    root = directory;
    if (const char *prior = std::getenv("XDG_CACHE_HOME"))
      priorCache = prior;
    require(::setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1) == 0,
            "Cannot isolate fixture cache");
  }
  ~Fixture() {
    if (priorCache)
      ::setenv("XDG_CACHE_HOME", priorCache->c_str(), 1);
    else
      ::unsetenv("XDG_CACHE_HOME");
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

demi::assets::AssetGroupProgress
wait(demi::runtime::RuntimeAssetService &service,
     demi::assets::AssetGroupRequestHandle request) {
  for (int attempt = 0; attempt < 2000; ++attempt) {
    service.update();
    const auto progress = service.progress(request);
    if (progress.stage == demi::assets::AssetGroupStage::Ready ||
        progress.stage == demi::assets::AssetGroupStage::Failed)
      return progress;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("Terrain asset service timed out");
}
} // namespace

int main() {
  try {
    Fixture fixture;
    demi::runtime::TerrainRecipe recipe;
    recipe.cellsX = recipe.cellsZ = 8;
    recipe.size = {16, 16};
    const auto sourcePath = fixture.root / "terrain.terrain.json";
    {
      std::ofstream source(sourcePath);
      source << demi::assets::terrainAssetSourceJson(
                    {.id = "asset://terrain/test", .recipe = recipe.toJson()})
                    .dump(2);
      require(source.good(), "Cannot write source fixture");
    }
    demi::AssetRegistry registry;
    registry.projectDirectory = fixture.root;
    registry.assets.push_back(
        {.id = "asset://terrain/test",
         .type = "Terrain",
         .importer = "terrain_heightfield",
         .importerVersion = 1,
         .sourceHash = *demi::assets::hashFiles({sourcePath}),
         .settingsJson = "{}",
         .sourcePath = sourcePath,
         .sourcePaths = {sourcePath}});
    demi::runtime::ProjectData project;
    project.projectDirectory = fixture.root;
    demi::runtime::RuntimeAssetService service;
    demi::Diagnostics diagnostics;
    require(service.configure(project, registry, &diagnostics),
            "Cannot configure service");
    const auto missing = wait(service, service.load("asset://terrain/test"));
    require(missing.stage == demi::assets::AssetGroupStage::Failed,
            "Runtime asset service generated missing terrain");
    require(!missing.error.empty(),
            "Missing prepared terrain had no diagnostic");
    (void)demi::assets::prepareTerrainAsset(registry, "asset://terrain/test");
    const auto loaded = wait(service, service.load("asset://terrain/test"));
    require(loaded.stage != demi::assets::AssetGroupStage::Failed,
            "Prepared terrain failed ordinary Assets.load");
    const auto report = service.memoryReport();
    require(report.assets.size() == 1 &&
                report.assets.front().backend == "terrain_cpu" &&
                report.residentBytes > 0,
            "Terrain used raw-source fallback or lost residency");
    require(!service.text("asset://terrain/test"),
            "Terrain was exposed as source text");
    require(service.unload("asset://terrain/test"),
            "Could not unload Terrain asset");
    require(service.memoryReport().assets.empty(),
            "Terrain residency survived unload");
    std::cout << "Terrain runtime asset service tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
