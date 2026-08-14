#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/assets/RegistryAssetResourceLoader.h"
#include "demi/runtime/assets/RuntimeAssetReload.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_runtime_assets_" + std::to_string(nonce));
  const auto source = root / "assets/fixture.bin";
  write(source, "resident fixture");
  const std::string hash = *demi::assets::hashFile(source);
  write(root / "assets/fixture.asset.json",
        nlohmann::json{{"format_version", 1},
                       {"id", "asset://fixture"},
                       {"type", "DataAsset"},
                       {"source", "fixture.bin"},
                       {"importer", "json_data"},
                       {"importer_version", 1},
                       {"source_hash", hash},
                       {"dependencies", nlohmann::json::array()},
                       {"settings", nlohmann::json::object()}}
            .dump(2));
  write(
      root / "scenes/main.scene.json",
      R"({"format_version":1,"id":"scene://main","name":"Main","entities":[{"id":"data","name":"Data","components":{"Sprite":{"texture":"asset://fixture"}}}]})");
  write(
      root / "scenes/next.scene.json",
      R"({"format_version":1,"id":"scene://next","name":"Next","entities":[{"id":"next-data","name":"Next Data","components":{"Sprite":{"texture":"asset://fixture"}}}]})");
  write(
      root / "groups/chapter.asset-group.json",
      R"({"format_version":1,"id":"asset-group://chapter","roots":["scene://main"],"budget":{"resident_mb":1,"decoded_mb":1,"upload_ms_per_frame":3}})");

  demi::runtime::ProjectData project;
  project.projectDirectory = root;
  project.projectPath = root / "demi.project.json";
  project.mainScene = "scene://main";
  project.scenes.push_back(
      {.id = "scene://main", .path = "scenes/main.scene.json"});
  project.scenes.push_back(
      {.id = "scene://next", .path = "scenes/next.scene.json"});
  demi::AssetRegistry registry = demi::loadAssetRegistry(root);
  assert(!demi::hasErrors(registry.diagnostics));

  demi::runtime::RuntimeAssetService assets;
  int backendApplications = 0;
  bool failNextBackendApplication = false;
  std::vector<std::string> backendAssets;
  assets.registerLoader(
      std::make_shared<demi::runtime::RegistryAssetResourceLoader>(
          registry, demi::runtime::RegistryAssetBackend{
                        .name = "test-native",
                        .assetTypes = {"DataAsset"},
                        .apply = [&](const demi::AssetRegistry &snapshot,
                                     std::string &backendError) {
                          ++backendApplications;
                          if (failNextBackendApplication) {
                            failNextBackendApplication = false;
                            backendError =
                                "Injected backend replacement failure.";
                            return false;
                          }
                          backendAssets.clear();
                          for (const auto &asset : snapshot.assets)
                            backendAssets.push_back(asset.id);
                          backendError.clear();
                          return true;
                        }}));
  demi::Diagnostics diagnostics;
  assert(assets.configure(project, registry, &diagnostics));
  const auto request = assets.prepare("asset-group://chapter", &diagnostics);
  assert(request != 0 && !demi::hasErrors(diagnostics));
  for (int attempt = 0;
       attempt < 1000 &&
       assets.progress(request).stage != demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.progress(request).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(assets.activate(request, &diagnostics));
  diagnostics.clear();
  assert(assets.load("asset-group://chapter", &diagnostics) == 0);
  assert(demi::hasErrors(diagnostics));
  diagnostics.clear();
  const auto report = assets.memoryReport();
  assert(report.assets.size() == 1);
  assert(report.assets.front().assetId == "asset://fixture");
  assert(report.assets.front().backend == "test-native");
  assert(report.assets.front().owners.contains("asset-group://chapter"));

  const int applicationsBeforeReload = backendApplications;
  demi::AssetRegistry previousRegistry = registry;
  registry.assets.front().sourceHash += "-watched";
  assert(assets.reloadChangedResidentAssets(previousRegistry, &diagnostics));
  assert(backendApplications > applicationsBeforeReload);
  assert(assets.restoreResources(&diagnostics));

  demi::AssetRegistry rejected = registry;
  rejected.assets.front().sourceHash += "-rejected";
  failNextBackendApplication = true;
  diagnostics.clear();
  assert(!demi::runtime::applyWatchedAssetRegistry(
      assets, registry, std::move(rejected), &diagnostics));
  assert(registry.assets.front().sourceHash ==
         previousRegistry.assets.front().sourceHash + "-watched");
  assert(!diagnostics.empty());

  diagnostics.clear();
  const auto lowMemoryRequest =
      assets.prepare("asset-group://chapter", &diagnostics);
  assert(lowMemoryRequest != 0);
  assets.handleLowMemory();
  assert(assets.progress(lowMemoryRequest).stage ==
         demi::assets::AssetGroupStage::Cancelled);
  assert(assets.unload("asset-group://chapter", &diagnostics));
  assert(assets.memoryReport().residentBytes == 0);

  diagnostics.clear();
  const auto assetRequest = assets.load("asset://fixture", &diagnostics);
  assert(assetRequest != 0 && !demi::hasErrors(diagnostics));
  for (int attempt = 0;
       attempt < 1000 && assets.progress(assetRequest).stage !=
                             demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.progress(assetRequest).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(assets.memoryReport().assets.front().owners.contains(
      "asset://fixture"));
  assert(assets.unload("asset://fixture", &diagnostics));
  assert(assets.memoryReport().residentBytes == 0);

  diagnostics.clear();
  const auto sceneRequest = assets.prepareScene("scene://main", &diagnostics);
  assert(sceneRequest != 0 && !demi::hasErrors(diagnostics));
  for (int attempt = 0;
       attempt < 1000 && assets.progress(sceneRequest).stage !=
                             demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.activate(sceneRequest, &diagnostics));
  assert(assets.memoryReport().assets.front().owners.contains(
      demi::runtime::RuntimeAssetService::sceneGroupId("scene://main")));
  assert(assets.releaseScene("scene://main", &diagnostics));
  assert(assets.memoryReport().residentBytes == 0);

  demi::runtime::World world;
  world.id = "scene://main";
  world.activeSceneId = "scene://main";
  world.loadedSceneIds = {"scene://main"};
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost lua;
  std::string error;
  assert(lua.initialize(world, input, nullptr, error));
  lua.setRuntimeAssetService(&assets);
  assert(lua.loadWorldScripts(project, world, error));
  const auto api = lua.publicLuaApi();
  assert(std::ranges::find(api, "Assets.load") != api.end());
  assert(std::ranges::find(api, "Assets.unload") != api.end());
  assert(std::ranges::find(api, "Assets.memory_report") != api.end());

  assert(lua.requestSceneLoad("scene://next"));
  bool activated = false;
  for (int attempt = 0; attempt < 1000 && !activated; ++attempt) {
    lua.update(0.0F);
    if (lua.hasPendingSceneLoad())
      activated = lua.applyPendingSceneLoad(error);
    std::this_thread::yield();
  }
  assert(activated && error.empty());
  assert(world.activeSceneId == "scene://next");
  const auto sceneReport = assets.memoryReport();
  assert(sceneReport.assets.size() == 1);
  assert(sceneReport.assets.front().owners.contains(
      demi::runtime::RuntimeAssetService::sceneGroupId("scene://next")));

  lua.destroy();
  assets.shutdown();
  std::filesystem::remove_all(root);
  return 0;
}
