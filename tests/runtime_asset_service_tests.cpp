#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/assets/HeadlessVisualAssetLoader.h"
#include "demi/runtime/assets/RegistryAssetResourceLoader.h"
#include "demi/runtime/assets/RuntimeAssetReload.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/assets/RuntimeAssetBootstrap.h"
#include "demi/runtime/scene/GenericComponent.h"
#include "demi/runtime/scene/ResourceLifetimeRegistry.h"
#include "demi/runtime/scene/SceneAssetReferences.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

void registryVerificationRetainsOnlyMetadata() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi_registry_stream_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto source = root / "large.bin";
  constexpr std::size_t sourceBytes = 1024U * 1024U;
  write(source, std::string(sourceBytes, 'x'));
  demi::AssetRegistry registry;
  for (int index = 0; index < 4; ++index) {
    registry.assets.push_back({.id = "asset://stream/" + std::to_string(index),
                               .type = "Texture2D",
                               .sourcePath = source,
                               .sourcePaths = {source}});
  }
  auto loader = std::make_shared<demi::runtime::RegistryAssetResourceLoader>(
      registry, demi::runtime::RegistryAssetBackend{
                    .name = "verification-probe",
                    .assetTypes = {"Texture2D"},
                    .apply = [](const demi::AssetRegistry &, std::string &) {
                      return true;
                    }});
  std::atomic_bool cancelled = false;
  std::string error;
  const auto decoded =
      loader->readAndDecode(registry.assets.front(), cancelled, error);
  assert(decoded && error.empty());
  assert(decoded->decodedBytes < 1024U);
  assert(decoded->residentBytes == sourceBytes);
  cancelled = true;
  assert(!loader->readAndDecode(registry.assets.front(), cancelled, error));
  {
    demi::assets::AssetGroupService service(registry);
    service.registerLoader(loader);
    demi::assets::AssetGroupDescriptor group{.id =
                                                 "asset-group://verification"};
    group.budget.maximumDecodedBytes = 1024U;
    for (const auto &asset : registry.assets) {
      group.roots.push_back(asset.id);
    }
    const auto request = service.prepare(group);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (service.progress(request).stage !=
               demi::assets::AssetGroupStage::Ready &&
           std::chrono::steady_clock::now() < deadline) {
      service.update();
      assert(service.progress(request).stage !=
             demi::assets::AssetGroupStage::Failed);
      std::this_thread::yield();
    }
    assert(service.activate(request));
    assert(service.memoryReport().residentBytes == 4U * sourceBytes);
    assert(service.releaseGroup(group.id));
  }
  std::filesystem::remove_all(root);
}

void stagedRegistryPublicationIsAtomicAndCancellable() {
  demi::AssetRegistry registry;
  for (const std::string id : {"asset://a", "asset://b", "asset://c"}) {
    registry.assets.push_back({.id = id, .type = "Texture2D"});
  }
  int steps = 0;
  int cancellations = 0;
  bool finish = false;
  bool fail = false;
  double progress = 0.0;
  std::vector<std::string> published;
  demi::runtime::RegistryAssetResourceLoader loader(
      registry, {.name = "staged-probe",
                 .assetTypes = {"Texture2D"},
                 .apply =
                     [&](const demi::AssetRegistry &candidate, std::string &) {
                       published.clear();
                       for (const auto &asset : candidate.assets) {
                         published.push_back(asset.id);
                       }
                       return true;
                     },
                 .applyStep =
                     [&](const demi::AssetRegistry &candidate, double,
                         std::string &error) {
                       ++steps;
                       if (fail) {
                         error = "Injected staged failure.";
                         return demi::runtime::AssetPublicationStage::Failed;
                       }
                       if (!finish) {
                         return demi::runtime::AssetPublicationStage::Pending;
                       }
                       published.clear();
                       for (const auto &asset : candidate.assets) {
                         published.push_back(asset.id);
                       }
                       return demi::runtime::AssetPublicationStage::Ready;
                     },
                 .cancelApply = [&] { ++cancellations; },
                 .applyProgress = [&] { return progress; }});
  const demi::assets::DecodedAsset decoded{.payload = std::make_shared<int>(1)};
  const std::array first{
      demi::assets::AssetResourceUpload{registry.assets[0], decoded},
      demi::assets::AssetResourceUpload{registry.assets[1], decoded}};
  const std::array other{
      demi::assets::AssetResourceUpload{registry.assets[2], decoded}};
  std::string error;
  assert(loader.uploadBatch(first, 3.0, error) == 0);
  assert(loader.hasPendingUploadBatch());
  const std::array firstIds{std::string("asset://a"), std::string("asset://b")};
  const std::array otherIds{std::string("asset://c")};
  progress = 0.6;
  assert(loader.pendingUploadProgress(firstIds) == 0.6);
  assert(loader.pendingUploadProgress(otherIds) == 0.0);
  assert(published.empty());
  assert(loader.uploadBatch(other, 3.0, error) == 0);
  assert(steps == 1);
  const std::array cancelled{std::string("asset://b")};
  loader.cancelUploadBatch(cancelled);
  assert(cancellations == 1);
  assert(!loader.hasPendingUploadBatch());
  assert(loader.pendingUploadProgress(firstIds) == 0.0);
  assert(published.empty());
  finish = true;
  assert(loader.uploadBatch(first, 3.0, error) == 2);
  assert(published == std::vector<std::string>({"asset://a", "asset://b"}));
  finish = false;
  assert(loader.uploadBatch(other, 3.0, error) == 0);
  fail = true;
  assert(loader.uploadBatch(other, 3.0, error) == 0);
  assert(error == "Injected staged failure.");
  assert(!loader.hasPendingUploadBatch());
  assert(loader.pendingUploadProgress(otherIds) == 0.0);
  assert(published == std::vector<std::string>({"asset://a", "asset://b"}));
  assert(loader.restore(error));
  assert(published == std::vector<std::string>({"asset://a", "asset://b"}));
}

void referenceCollectionSkipsInstancePayloadsAndRetainsDynamicResources() {
  demi::runtime::World world;
  demi::runtime::Entity instances;
  instances.id = "instances";
  instances.sceneOwner = "scene://references";
  nlohmann::json transforms = nlohmann::json::object();
  for (int index = 0; index < 4096; ++index) {
    transforms["asset://identity/instance_" + std::to_string(index)] = {
        {"position", {index, 0, index}}, {"scale", {1, 1, 1}}};
  }
  const auto payload = nlohmann::json{{"transforms", transforms}}.dump();
  instances.authoredComponents.push_back(
      std::make_shared<demi::runtime::GenericComponent>("MeshInstances3D",
                                                        payload));
  instances.serializedComponents["MeshInstances3D"] = payload;
  instances.authoredComponents.push_back(
      std::make_shared<demi::runtime::GenericComponent>(
          "Transform3D", R"({"parent":"asset://identity/parent"})"));
  world.entities.push_back(std::move(instances));
  demi::runtime::SceneAssetReferenceStatistics statistics;
  assert(demi::runtime::collectSceneAssetReferences(world, {}, &statistics)
             .empty());
  assert(statistics.skippedComponents == 3);
  assert(statistics.parsedPayloads == 0 && statistics.parsedPayloadBytes == 0);

  demi::runtime::Entity resources;
  resources.id = "prefab_instance/body";
  resources.prefabInstance = "prefab_instance";
  resources.sceneOwner = "scene://references";
  resources.authoredComponents.push_back(std::make_shared<
                                         demi::runtime::GenericComponent>(
      "MeshRenderer",
      R"({"model":"asset://models/full","medium_lod_model":"asset://models/mid","low_lod_model":"asset://models/low","material":"asset://materials/stone"})"));
  resources.serializedComponents["LuaScript"] =
      R"({"module":"game/props","properties":{"skin":"asset://textures/script","nested":["asset://data/script"]}})";
  resources.serializedComponents["GameplayData"] =
      R"({"values":{"recipe":"asset://data/recipe"}})";
  resources.serializedComponents["ProjectCustomComponent"] =
      R"({"nested":{"resource":"asset://custom/resource"}})";
  world.entities.push_back(std::move(resources));
  demi::runtime::ui::UiNode hud;
  hud.id = "hud";
  hud.sceneOwner = "scene://references";
  hud.font = "asset://hud/font";
  hud.texture = "asset://hud/image";
  hud.animation = "asset://hud/animation";
  world.ui.nodes.push_back(std::move(hud));
  const std::vector<std::string> entityAssets{
      "asset://custom/resource", "asset://data/recipe",
      "asset://data/script",     "asset://materials/stone",
      "asset://models/full",     "asset://models/low",
      "asset://models/mid",      "asset://textures/script"};
  const auto collectedEntities = demi::runtime::collectEntityAssetReferences(
      world.entities, "scene://references");
  assert(collectedEntities == entityAssets);
  auto sceneAssets = entityAssets;
  sceneAssets.insert(
      sceneAssets.end(),
      {"asset://hud/font", "asset://hud/image", "asset://hud/animation"});
  std::ranges::sort(sceneAssets);
  assert(demi::runtime::collectSceneAssetReferences(
             world, "scene://references") == sceneAssets);
  assert(demi::runtime::collectSceneAssetReferences(world, "scene://other")
             .empty());
  demi::runtime::ResourceLifetimeRegistry lifetime;
  lifetime.capture("scene://references", world.entities);
  for (const auto &asset : entityAssets) {
    assert(lifetime.owns("scene://references", asset));
  }
  assert(!lifetime.owns("scene://references", "asset://identity/parent"));
}

void headlessVisualPreparationDoesNotRetainSourceCopies() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_headless_visual_" + std::to_string(nonce));
  const auto visualSource = root / "visual-source.bin";
  constexpr std::size_t sourceBytes = 5U * 1024U * 1024U;
  write(visualSource, std::string(sourceBytes, 'v'));
  const auto dataSource = root / "record.json";
  write(dataSource,
        R"({"format_version":1,"value":"resident gameplay record"})");
  demi::AssetRegistry registry;
  registry.projectDirectory = root;
  demi::runtime::World world;
  world.activeSceneId = "scene://large_visual_scene";
  // Sixteen reachable visual roots total 80 MiB of encoded source estimates,
  // exceeding the unchanged implicit group's 64 MiB decoded-payload allowance.
  for (int index = 0; index < 16; ++index) {
    const auto id = "asset://headless/model_" + std::to_string(index);
    registry.assets.push_back({.id = id,
                               .type = "Model3D",
                               .sourcePath = visualSource,
                               .sourcePaths = {visualSource}});
    demi::runtime::Entity entity;
    entity.id = "model_" + std::to_string(index);
    entity.sceneOwner = world.activeSceneId;
    entity.serializedComponents["MeshRenderer"] =
        nlohmann::json{{"model", id}}.dump();
    world.entities.push_back(std::move(entity));
  }
  registry.assets.push_back({.id = "asset://headless/record",
                             .type = "DataAsset",
                             .sourcePath = dataSource,
                             .sourcePaths = {dataSource}});
  std::sort(
      registry.assets.begin(), registry.assets.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  world.entities.front().serializedComponents["GameplayData"] =
      R"({"values":{"record":"asset://headless/record"}})";
  demi::runtime::ProjectData project;
  project.projectDirectory = root;
  demi::runtime::RuntimeAssetService service;
  demi::Diagnostics diagnostics;
  assert(service.configure(project, registry, &diagnostics));
  const auto loader = demi::runtime::createHeadlessVisualAssetLoader(registry);
  for (const std::string type :
       {"Font2D", "GifAnimation2D", "Icon2D", "ImageAnimation2D", "Material",
        "Model3D", "RenderTarget", "Shader", "SvgTexture2D", "Texture2D",
        "Tilemap2D"}) {
    assert(loader->supports(demi::AssetManifest{.type = type}));
  }
  assert(!loader->supports(demi::AssetManifest{.type = "DataAsset"}));
  assert(!loader->supports(demi::AssetManifest{.type = "Text"}));
  service.registerLoader(loader);
  const auto request =
      service.prepareScene(world.activeSceneId, world, &diagnostics);
  for (const auto &issue : diagnostics) {
    std::cerr << issue.code << ": " << issue.message << '\n';
  }
  assert(request != 0 && !demi::hasErrors(diagnostics));
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (service.progress(request).stage !=
             demi::assets::AssetGroupStage::Ready &&
         std::chrono::steady_clock::now() < deadline) {
    service.update();
    assert(service.progress(request).stage !=
           demi::assets::AssetGroupStage::Failed);
    std::this_thread::yield();
  }
  assert(service.progress(request).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(service.activate(request, &diagnostics));
  assert(service.text("asset://headless/record", &diagnostics) ==
         std::optional<std::string>(
             R"({"format_version":1,"value":"resident gameplay record"})"));
  assert(!service.text("asset://headless/model_0"));
  const auto memory = service.memoryReport();
  assert(memory.assets.size() == 17);
  assert(memory.decodedBytes < 1024U);
  for (const auto &asset : memory.assets) {
    if (asset.assetId.starts_with("asset://headless/model_")) {
      assert(asset.backend == "headless-visual");
      assert(asset.residentBytes == sourceBytes);
    }
  }
  assert(service.releaseScene(world.activeSceneId));
  assert(service.memoryReport().residentBytes == 0);
  service.shutdown();
  std::filesystem::remove_all(root);
}

void registryBatchPublishesOneSnapshot() {
  demi::AssetRegistry registry;
  registry.assets.push_back({.id = "asset://a", .type = "Texture2D"});
  registry.assets.push_back({.id = "asset://b", .type = "Texture2D"});
  registry.assets.push_back({.id = "asset://c", .type = "Texture2D"});
  int applications = 0;
  bool reject = false;
  std::vector<std::string> resident;
  demi::runtime::RegistryAssetResourceLoader loader(
      registry,
      {.name = "batch-probe",
       .assetTypes = {"Texture2D"},
       .apply = [&](const demi::AssetRegistry &candidate, std::string &error) {
         ++applications;
         if (reject) {
           error = "Injected batch replacement failure.";
           return false;
         }
         resident.clear();
         for (const auto &asset : candidate.assets) {
           resident.push_back(asset.id);
         }
         return true;
       }});
  const demi::assets::DecodedAsset decoded{.payload = std::make_shared<int>(1)};
  const std::array uploads{
      demi::assets::AssetResourceUpload{registry.assets[0], decoded},
      demi::assets::AssetResourceUpload{registry.assets[1], decoded}};
  std::string error;
  assert(loader.uploadBatch(uploads, 0.0, error) == 2);
  assert(applications == 1);
  assert(resident == std::vector<std::string>({"asset://a", "asset://b"}));
  reject = true;
  const std::array rejected{
      demi::assets::AssetResourceUpload{registry.assets[2], decoded}};
  assert(loader.uploadBatch(rejected, -1.0, error) == 0);
  assert(error == "Injected batch replacement failure.");
  assert(resident == std::vector<std::string>({"asset://a", "asset://b"}));
  reject = false;
  assert(loader.restore(error));
  assert(resident == std::vector<std::string>({"asset://a", "asset://b"}));
  const std::array released{std::string("asset://a"), std::string("asset://b")};
  reject = true;
  assert(!loader.unloadBatch(released, error));
  assert(resident == std::vector<std::string>({"asset://a", "asset://b"}));
  reject = false;
  assert(loader.restore(error));
  assert(resident == std::vector<std::string>({"asset://a", "asset://b"}));
  const int applicationsBeforeRelease = applications;
  assert(loader.unloadBatch(released, error));
  assert(applications == applicationsBeforeRelease + 1);
  assert(resident.empty());
}

void registryGroupReleaseBatchesAndRetainsSharedResources() {
  demi::AssetRegistry registry;
  for (const std::string id : {"asset://a", "asset://b", "asset://c"}) {
    registry.assets.push_back({.id = id, .type = "Texture2D"});
  }
  int applications = 0;
  bool reject = false;
  std::vector<std::string> resident;
  auto loader = std::make_shared<demi::runtime::RegistryAssetResourceLoader>(
      registry, demi::runtime::RegistryAssetBackend{
                    .name = "release-probe",
                    .assetTypes = {"Texture2D"},
                    .apply = [&](const demi::AssetRegistry &candidate,
                                 std::string &error) {
                      ++applications;
                      if (reject) {
                        error = "Injected release failure.";
                        return false;
                      }
                      resident.clear();
                      for (const auto &asset : candidate.assets) {
                        resident.push_back(asset.id);
                      }
                      return true;
                    }});
  const auto prepare = [](demi::assets::AssetGroupService &service,
                          const demi::assets::AssetGroupDescriptor &group) {
    const auto request = service.prepare(group);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (service.progress(request).stage !=
               demi::assets::AssetGroupStage::Ready &&
           std::chrono::steady_clock::now() < deadline) {
      service.update();
      std::this_thread::yield();
    }
    assert(service.activate(request));
  };
  {
    demi::assets::AssetGroupService service(registry);
    service.registerLoader(loader);
    prepare(service,
            {.id = "asset-group://first", .roots = {"asset://a", "asset://b"}});
    prepare(service, {.id = "asset-group://second",
                      .roots = {"asset://b", "asset://c"}});
    reject = true;
    demi::Diagnostics diagnostics;
    assert(!service.releaseGroup("asset-group://first", &diagnostics));
    assert(service.isGroupActive("asset-group://first"));
    assert(demi::hasErrors(diagnostics));
    assert(service.memoryReport().assets.size() == 3);
    assert(resident ==
           std::vector<std::string>({"asset://a", "asset://b", "asset://c"}));
    reject = false;
    assert(service.releaseGroup("asset-group://first"));
    assert(resident == std::vector<std::string>({"asset://b", "asset://c"}));
    const int applicationsBeforeFinalRelease = applications;
    assert(service.releaseGroup("asset-group://second"));
    assert(applications == applicationsBeforeFinalRelease + 1);
    assert(resident.empty());
    assert(service.memoryReport().assets.empty());
  }
  int applicationsBeforeDestruction = 0;
  {
    demi::assets::AssetGroupService service(registry);
    service.registerLoader(loader);
    prepare(service, {.id = "asset-group://shutdown",
                      .roots = {"asset://a", "asset://b", "asset://c"}});
    applicationsBeforeDestruction = applications;
  }
  assert(applications == applicationsBeforeDestruction + 1);
  assert(resident.empty());
}

// Account a large decoded resource without allocating a large test payload.
class SceneBudgetProbeLoader final : public demi::assets::AssetResourceLoader {
public:
  bool supports(const demi::AssetManifest &) const override { return true; }
  std::optional<demi::assets::DecodedAsset>
  readAndDecode(const demi::AssetManifest &, const std::atomic_bool &,
                std::string &) override {
    return demi::assets::DecodedAsset{.decodedBytes = 65U * 1024U * 1024U,
                                      .residentBytes = 1};
  }
  bool upload(const demi::AssetManifest &, const demi::assets::DecodedAsset &,
              std::string &) override {
    return true;
  }
  void unload(std::string_view) override {}
  std::string_view backendName() const override { return "scene-budget-probe"; }
};

void implicitSceneBudgets(const std::filesystem::path &root,
                          const demi::runtime::ProjectData &project,
                          const demi::AssetRegistry &registry) {
  const auto policyPath = root / "groups/main-budget.asset-group.json";
  const auto run = [&](std::string_view sceneId,
                       const demi::runtime::World *prepared,
                       bool shouldSucceed, unsigned limitMiB = 64) {
    demi::runtime::RuntimeAssetService service;
    service.registerLoader(std::make_shared<SceneBudgetProbeLoader>());
    demi::Diagnostics diagnostics;
    assert(service.configure(project, registry, &diagnostics));
    const auto request = prepared
                             ? service.prepareScene(sceneId, *prepared,
                                                    &diagnostics)
                             : service.prepareScene(sceneId, &diagnostics);
    assert(request != 0 && !demi::hasErrors(diagnostics));
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    auto progress = service.progress(request);
    while (progress.stage != demi::assets::AssetGroupStage::Ready &&
           progress.stage != demi::assets::AssetGroupStage::Failed &&
           std::chrono::steady_clock::now() < deadline) {
      service.update();
      std::this_thread::yield();
      progress = service.progress(request);
    }
    if (shouldSucceed) {
      assert(progress.stage == demi::assets::AssetGroupStage::Ready);
      assert(progress.totalAssets == 1);
      assert(service.activate(request, &diagnostics));
      assert(service.releaseScene(sceneId, &diagnostics));
      assert(service.memoryReport().residentBytes == 0);
    } else {
      assert(progress.stage == demi::assets::AssetGroupStage::Failed);
      assert(progress.error.find("asset://fixture") != std::string::npos);
      assert(progress.error.find("68157440 bytes decoded") != std::string::npos);
      const auto expectedLimit =
          std::to_string(std::size_t{limitMiB} * 1024U * 1024U) + " bytes";
      assert(progress.error.find(expectedLimit) != std::string::npos);
    }
  };
  run("scene://main", nullptr, false);
  write(policyPath,
        R"({"format_version":1,"id":"asset-group://implicit-scene/main","roots":["scene://main"],"budget":{"decoded_mb":128,"resident_mb":1,"upload_ms_per_frame":2}})");
  run("scene://main", nullptr, true);
  run("scene://next", nullptr, false);
  demi::runtime::World prepared;
  demi::runtime::Entity entity;
  entity.id = "prepared-budget-reference";
  entity.sceneOwner = "scene://main";
  entity.serializedComponents["Sprite"] =
      R"({"texture":"asset://fixture"})";
  prepared.entities.push_back(std::move(entity));
  run("scene://main", &prepared, true);
  write(policyPath,
        R"({"format_version":1,"id":"asset-group://implicit-scene/main","roots":["scene://main"],"budget":{"decoded_mb":32}})");
  run("scene://main", nullptr, false, 32);
  run("scene://main", &prepared, false, 32);
  // The reserved namespace cannot silently replace scene discovery with
  // hand-maintained asset roots or another scene's roots.
  write(policyPath,
        R"({"format_version":1,"id":"asset-group://implicit-scene/main","roots":["asset://fixture"]})");
  demi::Diagnostics invalid;
  assert(!demi::assets::loadAssetGroup(policyPath, &invalid));
  assert(demi::hasErrors(invalid));
  std::filesystem::remove(policyPath);
}

} // namespace

void startupPumpsAndReusesPreparedWorld() {
  using namespace demi;
  using namespace demi::runtime;
  const auto root = std::filesystem::temp_directory_path() /
      ("demi-bootstrap-pump-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  write(root / "data.json", R"({"format_version":1,"value":42})");
  AssetRegistry registry;
  registry.projectDirectory = root;
  registry.assets.push_back({.id = "asset://bootstrap/data", .type = "DataAsset",
      .settingsJson = "{}", .sourcePath = root / "data.json", .sourcePaths = {root / "data.json"}});
  ProjectData project;
  project.projectDirectory = root;
  // No authored scene is registered. Startup must use the supplied world,
  // rather than trying to expand its source again on the platform thread.
  World world;
  world.activeSceneId = "scene://bootstrap/ready";
  Entity entity;
  entity.id = "owner";
  entity.sceneOwner = world.activeSceneId;
  entity.serializedComponents["GameplayData"] = R"({"values":{"resource":"asset://bootstrap/data"}})";
  world.entities.push_back(std::move(entity));
  RuntimeAssetService service;
  Diagnostics diagnostics;
  assert(service.configure(project, registry, &diagnostics));
  int pumps = 0;
  const auto result = prepareInitialSceneAssets(service, world.activeSceneId, {},
      &diagnostics, &world, [&](const auto &) { ++pumps; return true; });
  assert(result.success && result.hasResidentGroup && pumps > 0);
  assert(!hasErrors(diagnostics));
  assert(service.releaseScene(world.activeSceneId));
  diagnostics.clear();
  const auto cancelled = prepareInitialSceneAssets(service, world.activeSceneId, {},
      &diagnostics, &world, [](const auto &) { return false; });
  assert(!cancelled.success);
  assert(std::ranges::any_of(diagnostics, [](const auto &issue) {
    return issue.code == "ASSET_STARTUP_CANCELLED";
  }));
  assert(service.memoryReport().assets.empty());
  service.shutdown();
  std::filesystem::remove_all(root);
}

int main() {
  startupPumpsAndReusesPreparedWorld();
  headlessVisualPreparationDoesNotRetainSourceCopies();
  referenceCollectionSkipsInstancePayloadsAndRetainsDynamicResources();
  registryVerificationRetainsOnlyMetadata();
  stagedRegistryPublicationIsAtomicAndCancellable();
  registryBatchPublishesOneSnapshot();
  registryGroupReleaseBatchesAndRetainsSharedResources();
  {
    demi::runtime::World probe;
    demi::runtime::Entity entity;
    entity.id =
        "terrain/__scatter/asset://palette/woodland/scatter|tree|0|21/body";
    entity.serializedComponents["Transform3D"] =
        nlohmann::json{{"parent", entity.id},
                       {"asset://label/only", "ordinary label"}}
            .dump();
    entity.serializedComponents["MeshRenderer"] =
        nlohmann::json{{"model", "asset://models/high"},
                       {"medium_lod_model", "asset://models/medium"},
                       {"low_lod_model", "asset://models/low"},
                       {"material", "asset://materials/foliage"}}
            .dump();
    entity.serializedComponents["Terrain3D"] =
        nlohmann::json{{"scatter_exclusions", {entity.id}}}.dump();
    probe.entities.push_back(entity);
    assert(
        demi::extractAssetReferences(entity.serializedComponents["Terrain3D"])
            .empty());
    assert(
        demi::extractAssetReferences(entity.serializedComponents["Transform3D"])
            .empty());
    assert(demi::extractAssetReferences(
               R"(local model = "asset://models/scripted")") ==
           std::vector<std::string>({"asset://models/scripted"}));
    const auto references = demi::runtime::collectSceneAssetReferences(probe);
    assert(references ==
           std::vector<std::string>(
               {"asset://materials/foliage", "asset://models/high",
                "asset://models/low", "asset://models/medium"}));
  }
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
  const auto languageSource = root / "assets/language_en.yml";
  write(languageSource, "variables:\n  title: Keep\n");
  write(root / "assets/language_en.asset.json",
        nlohmann::json{{"format_version", 1},
                       {"id", "asset://language/en"},
                       {"type", "Text"},
                       {"source", "language_en.yml"},
                       {"importer", "copy"},
                       {"importer_version", 1},
                       {"source_hash", *demi::assets::hashFile(languageSource)},
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
  implicitSceneBudgets(root, project, registry);

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
  const auto textRequest = assets.load("asset://language/en", &diagnostics);
  assert(textRequest != 0);
  for (int attempt = 0;
       attempt < 1000 && assets.progress(textRequest).stage !=
                             demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  const auto residentText = assets.text("asset://language/en", &diagnostics);
  assert(residentText && *residentText == "variables:\n  title: Keep\n");
  assert(assets.unload("asset://language/en", &diagnostics));
  diagnostics.clear();
  assert(!assets.text("asset://language/en", &diagnostics));
  assert(demi::hasErrors(diagnostics));
  diagnostics.clear();
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
  assert(
      assets.memoryReport().assets.front().owners.contains("asset://fixture"));
  assert(assets.unload("asset://fixture", &diagnostics));
  assert(assets.memoryReport().residentBytes == 0);

  diagnostics.clear();
  const int applicationsBeforeScene = backendApplications;
  const auto sceneRequest = assets.prepareScene("scene://main", &diagnostics);
  assert(sceneRequest != 0 && !demi::hasErrors(diagnostics));
  assert(assets.progress(sceneRequest).totalAssets == 1);
  for (int attempt = 0;
       attempt < 1000 && assets.progress(sceneRequest).stage !=
                             demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.progress(sceneRequest).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(assets.activate(sceneRequest, &diagnostics));
  const auto sceneResidency = assets.memoryReport();
  assert(sceneResidency.assets.size() == 1);
  assert(sceneResidency.assets.front().assetId == "asset://fixture");
  assert(backendApplications == applicationsBeforeScene + 1);
  assert(sceneResidency.assets.front().owners.contains(
      demi::runtime::RuntimeAssetService::sceneGroupId("scene://main")));
  assert(assets.releaseScene("scene://main", &diagnostics));
  assert(assets.memoryReport().residentBytes == 0);

  // Prepared CPU worlds are authoritative even when no corresponding source
  // scene is registered. Resource discovery must not load that scene again.
  demi::runtime::World preparedWorld;
  preparedWorld.activeSceneId = "scene://prepared_only";
  demi::runtime::Entity preparedEntity;
  preparedEntity.id = "prepared-data";
  preparedEntity.sceneOwner = "scene://prepared_only";
  preparedEntity.serializedComponents["Sprite"] =
      nlohmann::json{{"texture", "asset://fixture"}}.dump();
  preparedWorld.entities.push_back(std::move(preparedEntity));
  const auto preparedRequest =
      assets.prepareScene("scene://prepared_only", preparedWorld, &diagnostics);
  assert(preparedRequest != 0 && !demi::hasErrors(diagnostics));
  assert(assets.progress(preparedRequest).totalAssets == 1);
  for (int attempt = 0;
       attempt < 1000 && assets.progress(preparedRequest).stage !=
                             demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.progress(preparedRequest).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(assets.activate(preparedRequest, &diagnostics));
  assert(assets.releaseScene("scene://prepared_only", &diagnostics));

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

  bool presentationReady = false;
  unsigned presentationPolls = 0;
  unsigned presentationResets = 0;
  lua.setScenePresentationPreparation(
      [&](const demi::runtime::World &incoming, bool &complete,
          std::string &presentationError) {
        assert(incoming.activeSceneId == "scene://next");
        assert(world.activeSceneId == "scene://main");
        ++presentationPolls;
        complete = presentationReady;
        presentationError.clear();
        return true;
      },
      [&] { ++presentationResets; });
  assert(lua.requestSceneLoad("scene://next"));
  bool activated = false;
  for (int attempt = 0; attempt < 1000 && !activated; ++attempt) {
    lua.beginFrame(0.0F);
    lua.update(0.0F);
    if (lua.hasPendingSceneLoad()) {
      activated = lua.applyPendingSceneLoad(error);
    }
    if (presentationPolls != 0 && !presentationReady) {
      const auto pollsThisFrame = presentationPolls;
      assert(!lua.scenePrepared());
      const float progress = lua.scenePreparationProgress();
      assert(progress > .66F && progress < .67F);
      assert(presentationPolls == pollsThisFrame);
      assert(world.activeSceneId == "scene://main");
      presentationReady = true;
    }
    std::this_thread::yield();
  }
  assert(activated && error.empty());
  assert(presentationPolls >= 2 && presentationResets == 1);
  assert(world.activeSceneId == "scene://next");
  const auto sceneReport = assets.memoryReport();
  assert(sceneReport.assets.size() == 1);
  assert(sceneReport.assets.front().owners.contains(
      demi::runtime::RuntimeAssetService::sceneGroupId("scene://next")));

  unsigned failedPresentationResets = 0;
  lua.setScenePresentationPreparation(
      [&](const demi::runtime::World &, bool &complete,
          std::string &presentationError) {
        complete = false;
        presentationError = "Injected presentation preparation failure";
        return false;
      },
      [&] { ++failedPresentationResets; });
  assert(lua.prepareScene("scene://main", false));
  for (int attempt = 0; attempt < 1000 && lua.sceneFlowError().empty();
       ++attempt) {
    lua.beginFrame(0.0F);
    lua.update(0.0F);
    assert(!lua.scenePrepared());
    std::this_thread::yield();
  }
  assert(lua.sceneFlowError() == "Injected presentation preparation failure");
  assert(world.activeSceneId == "scene://next");
  assert(failedPresentationResets == 1);
  assert(lua.cancelScenePreparation());

  unsigned pendingPresentationPolls = 0;
  unsigned cancelledPresentationResets = 0;
  lua.setScenePresentationPreparation(
      [&](const demi::runtime::World &, bool &complete,
          std::string &presentationError) {
        ++pendingPresentationPolls;
        complete = false;
        presentationError.clear();
        return true;
      },
      [&] { ++cancelledPresentationResets; });
  assert(lua.prepareScene("scene://main", false));
  for (int attempt = 0; attempt < 1000 && pendingPresentationPolls == 0;
       ++attempt) {
    lua.beginFrame(0.0F);
    lua.update(0.0F);
    assert(!lua.scenePrepared());
    std::this_thread::yield();
  }
  assert(pendingPresentationPolls == 1);
  assert(lua.preparedSceneWorld() != nullptr);
  assert(lua.cancelScenePreparation());
  assert(cancelledPresentationResets == 1);
  assert(lua.preparedSceneWorld() == nullptr);
  assert(world.activeSceneId == "scene://next");

  lua.destroy();
  assets.shutdown();
  std::filesystem::remove_all(root);
  return 0;
}
