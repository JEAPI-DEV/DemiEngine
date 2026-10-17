#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"
#include "demi/packages/PackageLock.h"
#include "demi/packages/PackageManifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>

namespace {
bool write(const std::filesystem::path &path, const char *text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  return static_cast<bool>(output);
}

bool testPackagePrefabResolution(const std::filesystem::path &root) {
  using demi::runtime::composition::resolvePrefabReference;
  using Json = nlohmann::json;
  const auto project = root / "package-project";
  write(project / "demi.project.json",
        R"({"format_version":1,"name":"Package prefabs"})");
  std::map<std::string, demi::packages::PackageRelease> releases;
  for (const std::string name :
       {"test.trees", "test.rocks", "test.unrelated"}) {
    Json manifest{{"format_version", 1},
                  {"name", name},
                  {"version", "1.0.0"},
                  {"engine", "^0.1.0"},
                  {"public_modules", Json::array()},
                  {"files", {"prefabs/model.prefab.json"}}};
    if (name == "test.trees") {
      manifest["dependencies"] = {{"test.rocks", "^1.0.0"}};
    }
    const auto packageRoot = project / ".demi/packages" / name;
    write(packageRoot / "demi.package.json", manifest.dump().c_str());
    const Json prefab{{"format_version", 1},
                      {"id", "prefab://" + name + "/model"},
                      {"entities", Json::array()}};
    write(packageRoot / "prefabs/model.prefab.json", prefab.dump().c_str());
    const auto loaded =
        demi::packages::loadPackageManifest(packageRoot / "demi.package.json");
    if (!loaded.manifest) {
      return false;
    }
    releases.emplace(name, demi::packages::PackageRelease{
                               .manifest = *loaded.manifest,
                               .archiveHash = "sha256:fixture",
                               .archiveUri = "package://" + name + "/1.0.0"});
  }
  const auto lock = demi::packages::packageLockJson(releases, "fixture");
  write(project / demi::packages::PackageLockFilename, lock.dump().c_str());
  const auto scenePath = project / "scenes/main.scene.json";
  const auto trees = project / ".demi/packages/test.trees";
  const auto treeOwner = trees / "prefabs/model.prefab.json";
  const auto rocks =
      project / ".demi/packages/test.rocks/prefabs/model.prefab.json";
  write(trees / "prefabs/private.prefab.json",
        R"({"format_version":1,"id":"prefab://private","entities":[]})");
  if (resolvePrefabReference(scenePath, "prefab://test.trees/model") !=
          treeOwner ||
      resolvePrefabReference(treeOwner, "prefab://test.trees/model") !=
          treeOwner ||
      resolvePrefabReference(treeOwner, "prefab://test.rocks/model") != rocks ||
      resolvePrefabReference(treeOwner, "prefab://test.unrelated/model") ||
      resolvePrefabReference(treeOwner, "prefab://private") ||
      resolvePrefabReference(scenePath, "prefab://../outside")) {
    std::cerr
        << "Source package prefab lookup ignored identity or visibility.\n";
    return false;
  }

  // Cooked packages have no installer manifests. The lock remains the owner
  // and dependency authority after relocation.
  std::filesystem::rename(project / ".demi/packages", project / "packages");
  for (const auto &[name, release] : releases) {
    (void)release;
    std::filesystem::remove(project / "packages" / name / "demi.package.json");
  }
  write(project / "cook.manifest.json", R"({"files":[],"assets":[]})");
  const auto cookedOwner =
      project / "packages/test.trees/prefabs/model.prefab.json";
  const auto cookedRocks =
      project / "packages/test.rocks/prefabs/model.prefab.json";
  if (resolvePrefabReference(scenePath, "prefab://test.trees/model") !=
          cookedOwner ||
      resolvePrefabReference(cookedOwner, "prefab://test.rocks/model") !=
          cookedRocks ||
      resolvePrefabReference(cookedOwner, "prefab://test.unrelated/model") ||
      resolvePrefabReference(cookedOwner, "prefab://private")) {
    std::cerr << "Cooked package prefab lookup lost dependency boundaries.\n";
    return false;
  }
  write(project / "packages/test.unrelated/prefabs/model.prefab.json",
        R"({"format_version":1,"id":"prefab://test.trees/model","entities":[]})");
  if (resolvePrefabReference(scenePath, "prefab://test.trees/model")) {
    std::cerr << "Duplicate package prefab identities were not rejected.\n";
    return false;
  }
  const auto localPrefab = project / "prefabs/test.trees/model.prefab.json";
  write(localPrefab,
        R"({"format_version":1,"id":"prefab://test.trees/model","entities":[]})");
  if (resolvePrefabReference(scenePath, "prefab://test.trees/model") != localPrefab) {
    std::cerr << "Project conventional prefab precedence was lost.\n";
    return false;
  }
  return true;
}
} // namespace

int main() {
  using demi::runtime::composition::expandScene;
  const auto nestedScene = nlohmann::json::parse(R"({
    "format_version":1,"id":"scene://nested","entities":[
      {"id":"wall","components":{"Transform3D":{}},"children":[
        {"id":"concrete","components":{"Transform3D":{"position":[0,1,0]}},"children":[
          {"id":"detail","components":{}}
        ]}
      ]}
    ]})");
  const auto nested = expandScene("nested.scene.json", nestedScene);
  if (!nested.document || (*nested.document)["entities"].size() != 3 ||
      (*nested.document)["entities"][1]["components"]["Transform3D"]["parent"] != "wall" ||
      (*nested.document)["entities"][2]["components"]["Transform3D"]["parent"] != "concrete" ||
      (*nested.document)["entities"][0].contains("children")) return 20;
  auto invalid = nestedScene;
  invalid["entities"][0]["children"][0]["components"]["Transform3D"]["parent"] = "other";
  if (expandScene("nested.scene.json", invalid).document) return 21;
  invalid = nestedScene;
  invalid["entities"][0]["children"] = "not an array";
  if (expandScene("nested.scene.json", invalid).document) return 22;
  invalid = nestedScene;
  invalid["entities"][0]["children"][0]["id"] = "wall";
  if (!demi::hasErrors(demi::validateSceneDocument("nested.scene.json", invalid))) return 23;
  invalid = nestedScene;
  invalid["entities"][0]["children"][0]["components"]["Transform3D"]["position"] = "bad";
  if (!demi::hasErrors(demi::validateSceneDocument("nested.scene.json", invalid))) return 24;
  for (const char *domain : {"Transform2D", "IsoTransform"}) {
    auto twoD = nestedScene;
    twoD["entities"][0]["components"] = {{domain, nlohmann::json::object()}};
    twoD["entities"][0]["children"][0]["components"] = {{domain, nlohmann::json::object()}};
    if (!expandScene("nested.scene.json", twoD).document) return 25;
  }
  const auto root =
      std::filesystem::temp_directory_path() / "demi_prefab_tests";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  write(root / "demi.project.json",
        R"({"format_version":1,"name":"Prefab tests"})");
  write(root / "prefabs/child.prefab.json", R"({
    "format_version":1,"id":"prefab://child","entities":[
      {"id":"mesh","name":"Mesh","components":{"Transform3D":{"position":[0,1,0]},"MeshRenderer":{"shape":"cube","size":[1,1,1]}}}
    ]
  })");
  write(root / "prefabs/parent.prefab.json", R"({
    "format_version":1,"id":"prefab://parent","entities":[
      {"id":"root","name":"Root","components":{"Transform3D":{"position":[0,0,0]},"Destructible3D":{"parts":{"piece":"child"}}},"children":[
      {"id":"child","name":"Child","components":{"Transform3D":{"position":[1,0,0]},"GameplayData":{"values":{"tags":[1,2,3],"keep":true}}}}]}
    ],"instances":[{"id":"nested","prefab":"prefab://child"}]
  })");

  const nlohmann::json scene = nlohmann::json::parse(R"({
    "format_version": 1,
    "id": "scene://test",
    "entities": [],
    "instances": [{
      "id": "hero",
      "prefab": "prefab://parent",
      "overrides": {
        "child": {
          "name": "Changed",
          "components": {
            "GameplayData": {"values": {"tags": [9], "keep": null}}
          }
        }
      }
    }]
  })");
  const auto expanded = expandScene(root / "scenes/main.scene.json", scene);
  if (!expanded.document || (*expanded.document)["entities"].size() != 3) {
    std::cerr << "Prefab expansion did not produce nested entities.\n";
    return 1;
  }
  const auto &child = (*expanded.document)["entities"][1];
  if ((*expanded.document)["entities"][0]["components"]["Destructible3D"]["parts"]["piece"] != "hero/child") {
    std::cerr << "Destructible visual references were not remapped inside the prefab.\n";
    return 1;
  }
  if (child["id"] != "hero/child" || child["name"] != "Changed" ||
      child["components"]["Transform3D"]["parent"] != "hero/root" ||
      child["components"]["GameplayData"]["values"]["tags"] !=
          nlohmann::json::array({9}) ||
      child["components"]["GameplayData"]["values"].contains("keep")) {
    std::cerr << "Prefab overrides or stable-id remapping failed.\n";
    return 1;
  }

  // Flattened override form: "entity.Component.field" alongside nesting.
  // Uses a fresh instance so earlier deep overrides cannot interfere.
  write(root / "prefabs/flat.prefab.json", R"({
    "format_version":1,"id":"prefab://flat","entities":[
      {"id":"wall","name":"Wall","components":{"Transform3D":{"position":[0,1,0]},"MeshRenderer":{"shape":"cube","size":[1,2,4]}}}
    ]
  })");
  const nlohmann::json flatScene = nlohmann::json::parse(R"({
    "format_version": 1,
    "id": "scene://flat",
    "entities": [],
    "instances": [{
      "id": "prop",
      "prefab": "prefab://flat",
      "overrides": {
        "wall.Transform3D.position": [4.0, 1.0, 0.0]
      }
    }]
  })");
  const auto flat = expandScene(root / "scenes/main.scene.json", flatScene);
  if (!flat.document || (*flat.document)["entities"].size() != 1) {
    std::cerr << "Flattened prefab overrides failed to expand.\n";
    return 1;
  }
  const auto &flatWall = (*flat.document)["entities"][0];
  for (const auto &key : {"missing", "missing.Transform3D.position"}) {
    auto invalid = flatScene;
    invalid["instances"][0]["overrides"] = {{key, nullptr}};
    const auto rejected = expandScene(root / "scenes/main.scene.json", invalid);
    if (rejected.document ||
        std::ranges::none_of(rejected.diagnostics, [](const auto &diagnostic) {
          return diagnostic.code == "PREFAB_OVERRIDE_TARGET_MISSING";
        })) {
      std::cerr << "Missing prefab override target was silently accepted.\n";
      return 1;
    }
  }
  if (flatWall["id"] != "prop/wall" ||
      flatWall["components"]["Transform3D"]["position"] !=
          nlohmann::json::array({4.0, 1.0, 0.0})) {
    std::cerr << "Flattened prefab overrides resolved incorrectly.\n";
    return 1;
  }

  auto mapped = flatScene;
  mapped["instances"][0]["entity_ids"] = {{"wall", "original_wall"}};
  const auto mappedResult =
      expandScene(root / "scenes/main.scene.json", mapped);
  if (!mappedResult.document ||
      (*mappedResult.document)["entities"][0]["id"] != "original_wall")
    return 40;
  const demi::runtime::composition::PrefabOriginIndex mappedOrigins(
      *mappedResult.document);
  const auto mappedOrigin = mappedOrigins.find("original_wall");
  if (!mappedOrigin || mappedOrigin->instanceId != "prop" ||
      mappedOrigin->localEntityId != "wall")
    return 41;
  for (const auto &badMap :
       {nlohmann::json::array(), nlohmann::json{{"missing", "wall"}},
        nlohmann::json{{"wall", ""}}, nlohmann::json{{"wall", 3}}}) {
    auto invalidMap = mapped;
    invalidMap["instances"][0]["entity_ids"] = badMap;
    if (expandScene(root / "scenes/main.scene.json", invalidMap).document)
      return 42;
  }
  auto collision = mapped;
  collision["entities"].push_back(
      {{"id", "original_wall"}, {"components", nlohmann::json::object()}});
  if (expandScene(root / "scenes/main.scene.json", collision).document)
    return 43;
  auto mappedPrefab = nlohmann::json{{"format_version", 1},
                                     {"id", "prefab://mapped"},
                                     {"entities", mapped["instances"]}};
  mappedPrefab["entities"].push_back(
      {{"id", "follower"},
       {"components", {{"Transform3D", {{"parent", "original_wall"}}}}}});
  write(root / "prefabs/mapped.prefab.json", mappedPrefab.dump().c_str());
  const auto nestedMapped = demi::runtime::composition::expandPrefabInstance(
      root / "scenes/main.scene.json",
      {{"id", "outer"}, {"prefab", "prefab://mapped"}});
  if (!nestedMapped.document ||
      (*nestedMapped.document)[0]["id"] != "outer/original_wall" ||
      (*nestedMapped.document)[1]["components"]["Transform3D"]["parent"] !=
          "outer/original_wall")
    return 44;

  write(
      root / "prefabs/cycle_a.prefab.json",
      R"({"format_version":1,"id":"prefab://cycle_a","entities":[],"instances":[{"id":"b","prefab":"prefab://cycle_b"}]})");
  write(
      root / "prefabs/cycle_b.prefab.json",
      R"({"format_version":1,"id":"prefab://cycle_b","entities":[],"instances":[{"id":"a","prefab":"prefab://cycle_a"}]})");
  const auto cycle = expandScene(
      root / "scenes/main.scene.json",
      nlohmann::json{
          {"format_version", 1},
          {"id", "scene://cycle"},
          {"instances", {{{"id", "cycle"}, {"prefab", "prefab://cycle_a"}}}}});
  if (cycle.document ||
      std::ranges::none_of(cycle.diagnostics, [](const auto &d) {
        return d.code == "PREFAB_CYCLE";
      })) {
    std::cerr << "Prefab cycles were not diagnosed.\n";
    return 1;
  }
  if (!testPackagePrefabResolution(root)) {
    return 45;
  }
  return 0;
}
