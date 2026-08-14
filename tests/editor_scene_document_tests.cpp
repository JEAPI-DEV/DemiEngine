#include "editor/EditorSceneDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_scene_document_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  const fs::path scene = root / "main.scene.json";
  write(scene, R"({
  "format_version": 1,
  "id": "scene://test/main",
  "name": "Main",
  "entities": [{
    "id": "ent_player",
    "name": "Player",
    "enabled": true,
    "components": {
      "Transform3D": {
        "position": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      }
    }
  }]
})");

  demi::editor::EditorSceneDocument document;
  std::string error;
  assert(document.open(scene, error));
  const demi::editor::SceneValueTarget position{.entityId = "ent_player",
                                                .component = "Transform3D",
                                                .field = "position"};
  assert(document.setValue(position, {1.0, 2.0, 3.0}, true, error));
  assert(document.setValue(position, {2.0, 3.0, 4.0}, true, error));
  document.endContinuousEdit();
  assert(document.isDirty());
  assert(document.canUndo());
  assert(document.undo(error));
  assert((*document.component("ent_player", "Transform3D"))["position"] ==
         nlohmann::json({0.0, 0.0, 0.0}));
  assert(document.redo(error));
  assert((*document.component("ent_player", "Transform3D"))["position"] ==
         nlohmann::json({2.0, 3.0, 4.0}));
  assert(!document.setValue(position, "not-a-vector", false, error));
  assert(!error.empty());

  error.clear();
  assert(document.save(error));
  assert(!document.isDirty());

  assert(document.setValue(position, {5.0, 6.0, 7.0}, false, error));
  write(scene, "{\n  \"format_version\": 1, \"entities\": []\n}\n");
  assert(!document.save(error));
  assert(error.find("changed on disk") != std::string::npos);

  fs::remove_all(root, ignored);
  return 0;
}
