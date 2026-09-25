#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/PrefabTemplateCache.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace demi;
using namespace demi::runtime;
using Json = nlohmann::json;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
  require(bool(output), "Fixture write failed: " + path.string());
}

void retain(PrefabTemplateCache &cache, const std::string &prefab,
            const Json &overrides, const Json &document) {
  const auto dependencies = cache.dependencies(prefab, overrides);
  require(dependencies.has_value(), "Could not collect template dependencies");
  cache.store(prefab, overrides, document, dependencies);
}

void verifyNestedEdits(const std::filesystem::path &root) {
  const auto child = root / "prefabs/child.prefab.json";
  write(child, R"({"format_version":1,"id":"prefab://child","entities":[]})");
  write(
      root / "prefabs/root.prefab.json",
      R"({"format_version":1,"id":"prefab://root","entities":[{"id":"nested","prefab":"prefab://child"}]})");

  PrefabTemplateCache cache;
  cache.configure(root);
  const Json overrides = {{"nested.color", {0.2, 0.3, 0.4, 1}}};
  const Json prepared = Json::array({{{"id", "template/body"}}});
  retain(cache, "prefab://root", overrides, prepared);
  require(cache.find("prefab://root", overrides) != nullptr,
          "Nested template missed");
  require(cache.find("prefab://root", Json::object()) == nullptr,
          "Different overrides shared a template");

  const auto before = cache.dependencies("prefab://root", overrides);
  write(
      child,
      R"({"format_version":1,"id":"prefab://child","entities":[],"name":"edited"})");
  cache.store("prefab://root", overrides, prepared, before);
  require(cache.find("prefab://root", overrides) == nullptr,
          "Child edit reused stale preparation");
  require(cache.statistics().bypasses == 1,
          "Concurrent preparation edit was not recorded");

  retain(cache, "prefab://root", overrides, prepared);
  require(cache.find("prefab://root", overrides),
          "Edited nested template could not be retained");
  cache.setCapacity(0);
  require(cache.statistics().entries == 0 &&
              !cache.find("prefab://root", overrides),
          "Disabling the cache retained templates");
}

void verifyAssetEdits(const std::filesystem::path &root) {
  const auto source = root / "mesh.gltf";
  write(
      source,
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":4}],"scenes":[{}],"scene":0})");
  write(root / "mesh.bin", "AAAA");
  const auto imported = assets::importAsset(
      {.projectDirectory = root, .source = source, .id = "asset://mesh"});
  require(!hasErrors(imported.diagnostics), "Model fixture import failed");
  const auto manifest = loadAssetManifest(imported.manifestPath);
  require(manifest.has_value(), "Imported model manifest is missing");
  const auto buffer = manifest->sourcePath.parent_path() / "mesh.bin";
  require(std::filesystem::is_regular_file(buffer),
          "External glTF buffer was not imported");
  write(
      root / "prefabs/model.prefab.json",
      R"({"format_version":1,"id":"prefab://model","entities":[{"id":"mesh","components":{"MeshRenderer":{"model":"asset://mesh"}}}]})");

  PrefabTemplateCache cache;
  cache.configure(root);
  const Json overrides = Json::object();
  const Json prepared = Json::array({{{"id", "template/mesh"}}});
  retain(cache, "prefab://model", overrides, prepared);
  require(cache.find("prefab://model", overrides), "Model template missed");

  // Content hashing must catch changes even when size and timestamp are
  // retained.
  const auto timestamp = std::filesystem::last_write_time(buffer);
  write(buffer, "BBBB");
  std::filesystem::last_write_time(buffer, timestamp);
  require(!cache.find("prefab://model", overrides),
          "External model buffer edit was missed");

  retain(cache, "prefab://model", overrides, prepared);
  write(root / "assets/new.asset.json", "{}");
  require(!cache.find("prefab://model", overrides),
          "Asset registry membership change was missed");
}

void verifyEviction(const std::filesystem::path &root) {
  PrefabTemplateCache cache;
  cache.configure(root);
  cache.setCapacity(2);
  const auto prefab = "prefab://child";
  const Json document = Json::array();
  const Json first = {{"choice", 1}};
  const Json second = {{"choice", 2}};
  const Json third = {{"choice", 3}};
  retain(cache, prefab, first, document);
  retain(cache, prefab, second, document);
  require(cache.find(prefab, first), "First entry missing before eviction");
  retain(cache, prefab, third, document);
  require(cache.statistics().entries == 2,
          "Retention capacity was not respected");
  require(!cache.find(prefab, second),
          "Least-recently-used template was not evicted");
  require(cache.find(prefab, first) && cache.find(prefab, third),
          "Eviction removed active entries");
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-template-cache-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    require(std::filesystem::create_directory(root),
            "Could not create owned test directory");
    write(root / "demi.project.json",
          R"({"format_version":1,"name":"Cache test"})");
    verifyNestedEdits(root);
    verifyEviction(root);
    verifyAssetEdits(root);
    std::filesystem::remove_all(root);
    std::cout
        << "Prefab dependency tracking, overrides, races and eviction passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << " fixture=" << root << '\n';
    return 1;
  }
}
