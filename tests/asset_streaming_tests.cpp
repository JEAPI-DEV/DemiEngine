#include "demi/assets/AssetCookGraph.h"
#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetGroup.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporterRegistry.h"
#include "demi/assets/GeneratedAtlasCooker.h"
#include "demi/packages/PackageLock.h"
#include "demi/packages/PackageManifest.h"
#include "demi/schema/Validation.h"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>

namespace {

class TestImporter final : public demi::assets::AssetImporter {
public:
  demi::assets::ImportExecutionResult
  import(const demi::assets::ImportExecutionRequest &) override {
    return {};
  }
};

class TestLoader final : public demi::assets::AssetResourceLoader {
public:
  bool supports(const demi::AssetManifest &) const override { return true; }

  std::optional<demi::assets::DecodedAsset>
  readAndDecode(const demi::AssetManifest &, const std::atomic_bool &,
                std::string &) override {
    ++decodeCount;
    return demi::assets::DecodedAsset{.payload = std::make_shared<int>(1),
                                      .decodedBytes = 16,
                                      .residentBytes = 32};
  }

  bool upload(const demi::AssetManifest &, const demi::assets::DecodedAsset &,
              std::string &) override {
    ++uploadCount;
    return true;
  }

  void unload(std::string_view assetId) override { unloaded.emplace(assetId); }

  std::string_view backendName() const override { return "test"; }

  int decodeCount = 0;
  int uploadCount = 0;
  std::set<std::string> unloaded;
};

void importerRegistryTest() {
  demi::assets::AssetImporterRegistry registry;
  demi::Diagnostics diagnostics;
  const auto factory = [] { return std::make_unique<TestImporter>(); };
  assert(registry.registerImporter({.name = "one",
                                    .extensions = {"fixture"},
                                    .assetTypes = {"Fixture"},
                                    .outputTypes = {"Fixture"}},
                                   factory, &diagnostics));
  assert(registry.select("content.fixture", "Fixture", {}, &diagnostics));
  assert(registry.create("one"));
  assert(registry.registerImporter({.name = "two",
                                    .extensions = {".fixture"},
                                    .assetTypes = {"Fixture"},
                                    .outputTypes = {"Fixture"}},
                                   factory, &diagnostics));
  diagnostics.clear();
  assert(!registry.select("content.fixture", "Fixture", {}, &diagnostics));
  assert(!diagnostics.empty() &&
         diagnostics.front().code == "ASSET_IMPORTER_AMBIGUOUS");
  diagnostics.clear();
  assert(registry.select("content.fixture", "Fixture", "two", &diagnostics));
}

demi::assets::AssetCookGraph graphWithVersion(int dependencyVersion,
                                              bool reverseOrder) {
  demi::assets::AssetCookGraph graph;
  demi::assets::AssetCookNode dependency{.assetId = "asset://dependency",
                                         .importer = "fixture",
                                         .importerVersion = dependencyVersion,
                                         .normalizedSettings =
                                             R"({"quality":80,"mipmaps":true})",
                                         .sourceHashes = {"source-b"},
                                         .platform = "linux"};
  demi::assets::AssetCookNode root{.assetId = "asset://root",
                                   .importer = "fixture",
                                   .normalizedSettings = R"({"mipmaps":false})",
                                   .sourceHashes = {"source-a"},
                                   .dependencies = {"asset://dependency"},
                                   .platform = "linux"};
  assert(reverseOrder ? graph.addNode(root) : graph.addNode(dependency));
  assert(reverseOrder ? graph.addNode(dependency) : graph.addNode(root));
  assert(graph.finalize());
  return graph;
}

void cookGraphTest() {
  auto first = graphWithVersion(1, false);
  auto reordered = graphWithVersion(1, true);
  auto changed = graphWithVersion(2, false);
  assert(first.key("asset://root") == reordered.key("asset://root"));
  assert(first.key("asset://root") != changed.key("asset://root"));
  assert(first.reverseReachable({"asset://dependency"}) ==
         std::set<std::string>({"asset://dependency", "asset://root"}));
}

void cookCacheTest() {
  const auto root = std::filesystem::temp_directory_path() /
                    "demi-asset-streaming-cache-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  const auto cookedOutput = root / "assets/shared.bin";
  {
    std::ofstream output(cookedOutput);
    output << "first";
  }
  demi::assets::AssetCookCache cache(root);
  demi::assets::AssetCookDecision miss{
      .assetId = "asset://shared", .key = "key-one", .outputs = {cookedOutput}};
  assert(!demi::hasErrors(cache.store(miss, "fixture.content", "content")));
  const auto hit = cache.inspect("asset://shared", "key-one");
  assert(hit.isCacheHit && hit.reason == "key_and_outputs_match");
  {
    std::ofstream output(cookedOutput);
    output << "corrupt";
  }
  const auto corrupt = cache.inspect("asset://shared", "key-one");
  assert(!corrupt.isCacheHit && corrupt.reason == "output_missing_or_corrupt");
  std::filesystem::remove_all(root);
}

void groupLifetimeTest() {
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://shared", .type = "Texture2D", .dependencies = {}});
  auto loader = std::make_shared<TestLoader>();
  demi::assets::AssetGroupService service(registry);
  service.registerLoader(loader);
  demi::assets::AssetGroupDescriptor first{.id = "asset-group://first",
                                           .roots = {"asset://shared"}};
  demi::assets::AssetGroupDescriptor second{.id = "asset-group://second",
                                            .roots = {"asset://shared"}};
  const auto firstRequest = service.prepare(first);
  for (int attempt = 0;
       attempt < 100 && service.progress(firstRequest).stage !=
                            demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    service.update();
    std::this_thread::yield();
  }
  assert(service.activate(firstRequest));
  assert(service.isGroupActive(first.id));
  const auto secondRequest = service.prepare(second);
  service.update();
  assert(service.progress(secondRequest).fraction == 1.0);
  assert(service.activate(secondRequest));
  assert(loader->decodeCount == 1 && loader->uploadCount == 1);
  assert(service.reload("asset://shared"));
  assert(loader->decodeCount == 2 && loader->uploadCount == 2);
  assert(service.memoryReport().assets.size() == 1);
  assert(service.releaseGroup(first.id));
  assert(!service.isGroupActive(first.id));
  assert(loader->unloaded.empty());
  assert(service.releaseGroup(second.id));
  assert(loader->unloaded == std::set<std::string>{"asset://shared"});
  assert(service.memoryReport().residentBytes == 0);
}

void concurrentGroupDeduplicationTest() {
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://shared", .type = "Texture2D", .dependencies = {}});
  auto loader = std::make_shared<TestLoader>();
  demi::assets::AssetGroupService service(registry);
  service.registerLoader(loader);
  const demi::assets::AssetGroupDescriptor first{
      .id = "asset-group://concurrent-first", .roots = {"asset://shared"}};
  const demi::assets::AssetGroupDescriptor second{
      .id = "asset-group://concurrent-second", .roots = {"asset://shared"}};
  const auto firstRequest = service.prepare(first);
  const auto secondRequest = service.prepare(second);
  assert(service.cancel(firstRequest));
  for (int attempt = 0;
       attempt < 100 && service.progress(secondRequest).stage !=
                            demi::assets::AssetGroupStage::Ready;
       ++attempt) {
    service.update();
    std::this_thread::yield();
  }
  assert(service.progress(firstRequest).stage ==
         demi::assets::AssetGroupStage::Cancelled);
  assert(service.progress(secondRequest).stage ==
         demi::assets::AssetGroupStage::Ready);
  assert(loader->decodeCount == 1 && loader->uploadCount == 1);
  assert(service.activate(secondRequest));
  assert(service.releaseGroup(second.id));
}

void sceneRootGroupValidationTest() {
  const auto root = std::filesystem::temp_directory_path() /
                    "demi-scene-root-group-validation-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "scenes");
  std::filesystem::create_directories(root / "assets/groups");
  const auto groupPath = root / "assets/groups/chapter.asset-group.json";
  {
    std::ofstream project(root / "demi.project.json");
    project
        << R"({"format_version":1,"name":"Fixture","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})";
    std::ofstream scene(root / "scenes/main.scene.json");
    scene
        << R"({"format_version":1,"id":"scene://main","name":"Main","entities":[]})";
    std::ofstream group(groupPath);
    group
        << R"({"format_version":1,"id":"asset-group://chapter","roots":["scene://main"],"budget":{"resident_mb":1,"upload_ms_per_frame":1}})";
  }
  const auto valid = demi::validatePath(groupPath);
  assert(!demi::hasErrors(valid.diagnostics));

  const auto cooked = root / "generated/cooked";
  const auto cookDiagnostics =
      demi::assets::cookProject({.projectFile = root / "demi.project.json",
                                 .outputDirectory = cooked,
                                 .platform = "linux"});
  assert(!demi::hasErrors(cookDiagnostics));
  assert(std::filesystem::is_regular_file(
      cooked / "assets/groups/chapter.asset-group.json"));

  {
    std::ofstream group(groupPath);
    group
        << R"({"format_version":1,"id":"asset-group://chapter","roots":["scene://missing"],"budget":{"resident_mb":1,"upload_ms_per_frame":1}})";
  }
  const auto invalid = demi::validatePath(groupPath);
  assert(std::ranges::any_of(invalid.diagnostics, [](const auto &diagnostic) {
    return diagnostic.code == "ASSET_GROUP_SCENE_ROOT_NOT_FOUND";
  }));
  std::filesystem::remove_all(root);
}

void packageContentManifestTest() {
  const nlohmann::json document{
      {"format_version", 1},
      {"name", "fixture.content"},
      {"version", "1.0.0"},
      {"engine", "*"},
      {"dependencies", nlohmann::json::object()},
      {"public_modules", nlohmann::json::array()},
      {"exported_events", nlohmann::json::array()},
      {"files", {"assets/shared.asset.json", "extensions/importer.json"}},
      {"asset_manifests", {"assets/shared.asset.json"}},
      {"engine_extensions", {"extensions/importer.json"}}};
  const auto parsed = demi::packages::parsePackageManifest(document, "fixture");
  assert(parsed.manifest && !demi::hasErrors(parsed.diagnostics));
  assert(parsed.manifest->assetManifests.size() == 1);
  assert(demi::packages::packageManifestJson(*parsed.manifest) == document);
}

void lockedPackageCookRoundTripTest() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi-locked-content-cook-" + std::to_string(nonce));
  const auto packageRoot = root / ".demi/packages/fixture.content";
  std::filesystem::create_directories(root / "scenes");
  std::filesystem::create_directories(packageRoot / "assets");

  const auto writeJson = [](const std::filesystem::path &path,
                            const nlohmann::json &document) {
    std::ofstream output(path);
    output << document.dump(2) << '\n';
  };
  writeJson(root / "demi.project.json",
            {{"format_version", 1},
             {"name", "Locked content cook fixture"},
             {"main_scene", "scene://main"},
             {"scenes",
              {{{"id", "scene://main"}, {"path", "scenes/main.scene.json"}}}}});
  writeJson(root / "scenes/main.scene.json",
            {{"format_version", 1},
             {"id", "scene://main"},
             {"name", "Main"},
             {"entities", nlohmann::json::array()}});
  writeJson(packageRoot / "assets/content.json", {{"value", 7}});
  const std::string sourceHash =
      *demi::assets::hashFile(packageRoot / "assets/content.json");
  writeJson(packageRoot / "assets/content.asset.json",
            {{"format_version", 1},
             {"id", "asset://fixture/content"},
             {"type", "DataAsset"},
             {"source", "content.json"},
             {"importer", "json_data"},
             {"importer_version", 1},
             {"source_hash", sourceHash},
             {"dependencies", nlohmann::json::array()},
             {"settings", nlohmann::json::object()}});

  const nlohmann::json packageDocument{
      {"format_version", 1},
      {"name", "fixture.content"},
      {"version", "1.0.0"},
      {"engine", "*"},
      {"dependencies", nlohmann::json::object()},
      {"public_modules", nlohmann::json::array()},
      {"exported_events", nlohmann::json::array()},
      {"files", {"assets/content.json", "assets/content.asset.json"}},
      {"asset_manifests", {"assets/content.asset.json"}}};
  writeJson(packageRoot / demi::packages::PackageManifestFilename,
            packageDocument);
  const auto parsed =
      demi::packages::parsePackageManifest(packageDocument, "fixture");
  assert(parsed.manifest && !demi::hasErrors(parsed.diagnostics));
  const demi::packages::PackageRelease release{
      .manifest = *parsed.manifest,
      .archiveHash = "sha256:fixture",
      .archiveUri = "file:///offline/fixture-content.demi-package"};
  writeJson(root / demi::packages::PackageLockFilename,
            demi::packages::packageLockJson({{release.manifest.name, release}},
                                            "offline-test"));

  const auto cook = [&](const std::string &platform,
                        const std::filesystem::path &output) {
    const auto diagnostics =
        demi::assets::cookProject({.projectFile = root / "demi.project.json",
                                   .outputDirectory = output,
                                   .platform = platform});
    assert(!demi::hasErrors(diagnostics));
    return nlohmann::json::parse(std::ifstream(output / "cook.manifest.json"));
  };
  const nlohmann::json firstLinux =
      cook("linux", root / "generated/cooked-linux-a");
  const nlohmann::json secondLinux =
      cook("linux", root / "generated/cooked-linux-b");
  const nlohmann::json android =
      cook("android", root / "generated/cooked-android");
  assert(firstLinux == secondLinux);
  assert(android.value("platform", "") == "android");
  for (const nlohmann::json *manifest : {&firstLinux, &android}) {
    const auto asset = std::ranges::find_if(
        (*manifest)["assets"], [](const nlohmann::json &entry) {
          return entry.value("asset", "") == "asset://fixture/content";
        });
    assert(asset != (*manifest)["assets"].end());
    assert(asset->value("source_package", "") == "fixture.content");
    assert(!asset->value("package_content_hash", "").empty());
  }
  std::filesystem::remove_all(root);
}

void generatedAtlasTest() {
  constexpr std::array<unsigned char, 90> png = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00,
      0x21, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x48, 0x70, 0x35, 0xf8,
      0x4f, 0x09, 0x66, 0x00, 0x11, 0x2d, 0x71, 0x4e, 0x64, 0xe1, 0x51, 0x03,
      0x46, 0x0d, 0x18, 0x35, 0x80, 0xda, 0x06, 0x50, 0x82, 0x01, 0x06, 0xd5,
      0x10, 0x9b, 0x19, 0xdd, 0x52, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
      0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  const auto root =
      std::filesystem::temp_directory_path() / "demi-generated-atlas-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto imagePath = root / "sprite.png";
  {
    std::ofstream output(imagePath, std::ios::binary);
    output.write(reinterpret_cast<const char *>(png.data()), png.size());
  }
  const auto descriptorPath = root / "atlas.json";
  {
    std::ofstream output(descriptorPath);
    output << nlohmann::json{{"format_version", 1},
                             {"page_width", 32},
                             {"page_height", 32},
                             {"padding", 2},
                             {"bleed", 1},
                             {"sprites",
                              {{{"id", "asset://sprites/fixture"},
                                {"source", "asset://textures/fixture"},
                                {"pivot", {0.5, 0.5}},
                                {"border", {1, 1, 1, 1}},
                                {"animation_tag", "idle"}}}}}
                  .dump(2);
  }
  demi::AssetRegistry registry;
  registry.assets.push_back({.id = "asset://textures/fixture",
                             .type = "Texture2D",
                             .sourcePath = imagePath,
                             .sourcePaths = {imagePath}});
  const demi::AssetManifest atlas{.id = "asset://atlases/fixture",
                                  .type = "TextureAtlas2D",
                                  .importer = "texture_atlas",
                                  .dependencies = {"asset://textures/fixture"},
                                  .manifestPath = root / "atlas.asset.json",
                                  .sourcePath = descriptorPath,
                                  .sourcePaths = {descriptorPath}};
  const auto generated =
      demi::assets::cookGeneratedAtlas(atlas, registry, root / "cooked");
  assert(!demi::hasErrors(generated.diagnostics));
  assert(generated.outputs.size() == 2);
  const auto metadata = nlohmann::json::parse(std::ifstream(
      root / "cooked/generated/atlases/atlases/fixture/atlas.json"));
  assert(metadata["sprites"][0]["rect"] == nlohmann::json({2, 2, 16, 16}));
  assert(metadata["sprites"][0]["animation_tag"] == "idle");

  const auto fontPath = std::filesystem::path(DEMI_SOURCE_DIR) /
                        "fonts/Pixelify_Sans/static/PixelifySans-Regular.ttf";
  const demi::AssetManifest font{
      .id = "asset://fonts/fixture",
      .type = "FontAtlas2D",
      .importer = "font",
      .settingsJson =
          R"({"pixel_height":20,"page_size":128,"glyph_ranges":["U+0041-U+0043"]})",
      .manifestPath = root / "font.asset.json",
      .sourcePath = fontPath,
      .sourcePaths = {fontPath}};
  const auto generatedFont =
      demi::assets::cookGeneratedAtlas(font, registry, root / "cooked");
  assert(!demi::hasErrors(generatedFont.diagnostics));
  assert(generatedFont.outputs.size() == 2);
  const auto fontMetadata = nlohmann::json::parse(std::ifstream(
      root / "cooked/generated/atlases/fonts/fixture/font-atlas.json"));
  assert(fontMetadata["glyphs"].size() == 3);
  std::filesystem::remove_all(root / "cooked");

  {
    std::filesystem::create_directories(root / "assets");
    std::filesystem::copy_file(imagePath, root / "assets/sprite.png");
    std::filesystem::copy_file(descriptorPath, root / "assets/atlas.json");
    std::ofstream project(root / "demi.project.json");
    project << R"({"format_version":1,"name":"Atlas Cook","scenes":[]})";
    std::ofstream textureManifest(root / "assets/texture.asset.json");
    textureManifest << nlohmann::json{{"format_version", 1},
                                      {"id", "asset://textures/fixture"},
                                      {"type", "Texture2D"},
                                      {"source", "sprite.png"},
                                      {"importer", "image"},
                                      {"importer_version", 1},
                                      {"source_hash",
                                       *demi::assets::hashFiles(
                                           {root / "assets/sprite.png"})},
                                      {"dependencies", nlohmann::json::array()},
                                      {"settings", nlohmann::json::object()}}
                           .dump(2);
    std::ofstream atlasManifest(root / "assets/atlas.asset.json");
    atlasManifest << nlohmann::json{{"format_version", 1},
                                    {"id", "asset://atlases/fixture"},
                                    {"type", "TextureAtlas2D"},
                                    {"source", "atlas.json"},
                                    {"importer", "texture_atlas"},
                                    {"importer_version", 2},
                                    {"source_hash",
                                     *demi::assets::hashFiles(
                                         {root / "assets/atlas.json"})},
                                    {"dependencies",
                                     {"asset://textures/fixture"}},
                                    {"settings", nlohmann::json::object()}}
                         .dump(2);
  }
  const auto cookedProject = root / "project-cook";
  auto cookDiagnostics =
      demi::assets::cookProject({.projectFile = root / "demi.project.json",
                                 .outputDirectory = cookedProject,
                                 .platform = "linux"});
  assert(!demi::hasErrors(cookDiagnostics));
  assert(std::filesystem::is_regular_file(
      cookedProject / "generated/atlases/atlases/fixture/atlas-0.png"));
  assert(std::filesystem::is_regular_file(
      cookedProject / "generated/atlases/atlases/fixture/atlas.json"));
  cookDiagnostics =
      demi::assets::cookProject({.projectFile = root / "demi.project.json",
                                 .outputDirectory = cookedProject,
                                 .platform = "linux"});
  assert(!demi::hasErrors(cookDiagnostics));
  const auto cacheReport = nlohmann::json::parse(
      std::ifstream(cookedProject / ".cook-cache/last-report.json"));
  const auto atlasDecision =
      std::ranges::find_if(cacheReport["assets"], [](const auto &entry) {
        return entry.value("asset", "") == "asset://atlases/fixture";
      });
  assert(atlasDecision != cacheReport["assets"].end());
  assert(atlasDecision->value("cache_hit", false));
  {
    nlohmann::json invalidDescriptor =
        nlohmann::json::parse(std::ifstream(root / "assets/atlas.json"));
    invalidDescriptor["padding"] = 0;
    invalidDescriptor["bleed"] = 1;
    std::ofstream output(root / "assets/atlas.json");
    output << invalidDescriptor.dump(2);
  }
  const auto invalidAtlasRegistry = demi::loadAssetRegistry(root);
  const auto invalidAtlasDiagnostics =
      demi::validateAssetRegistry(invalidAtlasRegistry);
  assert(
      std::ranges::any_of(invalidAtlasDiagnostics, [](const auto &diagnostic) {
        return diagnostic.code == "ATLAS_SETTINGS_INVALID";
      }));
  std::filesystem::remove_all(root);
}

} // namespace

int main() {
  importerRegistryTest();
  cookGraphTest();
  cookCacheTest();
  groupLifetimeTest();
  concurrentGroupDeduplicationTest();
  sceneRootGroupValidationTest();
  packageContentManifestTest();
  lockedPackageCookRoundTripTest();
  generatedAtlasTest();
  return 0;
}
