#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>

namespace {
bool write(const std::filesystem::path &path, const char *text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  return static_cast<bool>(output);
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
  if (flatWall["id"] != "prop/wall" ||
      flatWall["components"]["Transform3D"]["position"] !=
          nlohmann::json::array({4.0, 1.0, 0.0})) {
    std::cerr << "Flattened prefab overrides resolved incorrectly.\n";
    return 1;
  }

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
  return 0;
}
