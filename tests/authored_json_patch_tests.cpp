#include "editor/EditorAuthoredJson.h"
#include "editor/EditorSceneDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  using namespace demi::editor;
  const std::string source = R"({
  "format_version": 1,
  "id": "scene://format/test",
  "entities": [
    {
      "id": "spawn",
      "name": "Spawn",
      "components": {
        "IsoTransform": {
          "tile": [1, 9],
          "height": 0.0,
          "footprint": [1, 1]
        }
      }
    }
  ]
}
)";
  const nlohmann::json before = nlohmann::json::parse(source);
  nlohmann::json raw = nlohmann::json::array({0.8154292106628418, 9.0});
  const nlohmann::json normalized = normalizeEditorAuthoredValue(
      std::move(raw),
      &before["entities"][0]["components"]["IsoTransform"]["tile"]);
  assert(normalized == nlohmann::json::array({0.815, 9}));
  nlohmann::json after = before;
  after["entities"][0]["components"]["IsoTransform"]["tile"] = normalized;
  const auto patched = patchEditorJsonSource(source, before, after);
  assert(patched);
  std::string expected = source;
  expected.replace(expected.find("[1, 9]"), 6, "[0.815, 9]");
  assert(*patched == expected);
  nlohmann::json structural = after;
  structural["entities"].push_back({{"components", nlohmann::json::object()},
                                    {"id", "new"},
                                    {"name", "New Entity"}});
  const auto added = patchEditorJsonSource(expected, after, structural);
  assert(added && nlohmann::json::parse(*added) == structural);
  const std::size_t entityBegin =
      expected.find("    {\n      \"id\": \"spawn\"");
  const std::size_t entityEnd = expected.rfind("\n    }\n  ]") + 6;
  const std::string originalEntity =
      expected.substr(entityBegin, entityEnd - entityBegin);
  assert(added->find(originalEntity) != std::string::npos);
  const std::size_t newEntity = added->find("\"id\": \"new\"");
  assert(newEntity != std::string::npos);
  assert(newEntity < added->find("\"name\": \"New Entity\"", newEntity));
  assert(added->find("\"name\": \"New Entity\"", newEntity) <
         added->find("\"components\": {}", newEntity));

  nlohmann::json withComponent = structural;
  withComponent["entities"][1]["components"]["IsoTransform"] = {
      {"tile", nlohmann::json::array({12.0, 11.0})},
      {"height", 0.0},
      {"footprint", nlohmann::json::array({0.0, 0.0})}};
  const auto componentAdded =
      patchEditorJsonSource(*added, structural, withComponent);
  assert(componentAdded && nlohmann::json::parse(*componentAdded) ==
                               withComponent);
  assert(componentAdded->find(
             "      \"components\": {\n"
             "        \"IsoTransform\": {\n"
             "          \"tile\": [12.0, 11.0],\n"
             "          \"height\": 0.0,\n"
             "          \"footprint\": [0.0, 0.0]\n"
             "        }\n"
             "      }") != std::string::npos);
  const auto removed = patchEditorJsonSource(*added, structural, after);
  assert(removed && nlohmann::json::parse(*removed) == after);

  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "demi_editor_authored_json";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  const fs::path scene = root / "main.scene.json";
  {
    std::ofstream output(scene);
    output << source;
  }
  EditorSceneDocument document;
  std::string error;
  assert(document.open(scene, error));
  assert(document.setValue(
      {.entityId = "spawn", .component = "IsoTransform", .field = "tile"},
      nlohmann::json::array({0.8154292106628418, 9.0}), false, error));
  assert(document.save(error));
  assert(read(scene) == expected);
  assert(document.createEntity(error));
  assert(document.save(error));
  const std::string withEntity = read(scene);
  assert(withEntity.find(originalEntity) != std::string::npos);
  assert(nlohmann::json::parse(withEntity) == document.json());
  fs::remove_all(root, ignored);
}
