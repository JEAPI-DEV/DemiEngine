#include "editor/EditorSceneDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__unix__)
#include <unistd.h>
#endif

namespace {

constexpr const char *kScene = R"({
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
      },
      "GameplayData": {
        "values": {}
      }
    }
  }]
})";

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
  write(scene, kScene);

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

  // Cancelling a drag restores both the canonical value and any redo branch
  // that existed before the temporary continuous edit began.
  assert(document.undo(error));
  const std::string beforeCancelledDrag = document.json().dump();
  assert(document.canRedo());
  assert(document.setValue(position, {8.0, 9.0, 10.0}, true, error));
  assert(document.setValue(position, {9.0, 10.0, 11.0}, true, error));
  assert(document.cancelContinuousEdit(error));
  assert(document.json().dump() == beforeCancelledDrag);
  assert(document.canRedo());
  assert(document.redo(error));
  assert((*document.component("ent_player", "Transform3D"))["position"] ==
         nlohmann::json({2.0, 3.0, 4.0}));
  // Optional field insertion/removal preserves authored presence exactly.
  const demi::editor::SceneValueTarget layer{.entityId = "ent_player",
                                             .field = "layer"};
  assert(!document.entity("ent_player")->contains("layer"));
  assert(document.setValue(layer, "Gameplay", false, error));
  assert(document.entity("ent_player")->at("layer") == "Gameplay");
  assert(document.undo(error));
  assert(!document.entity("ent_player")->contains("layer"));
  assert(document.redo(error));
  assert(document.removeValue(layer, error));
  assert(!document.entity("ent_player")->contains("layer"));
  assert(document.undo(error));
  assert(document.entity("ent_player")->at("layer") == "Gameplay");
  assert(document.redo(error));

  assert(document.removeValue(position, error));
  assert(
      !document.component("ent_player", "Transform3D")->contains("position"));
  assert(document.undo(error));
  assert(document.component("ent_player", "Transform3D")->at("position") ==
         nlohmann::json({2.0, 3.0, 4.0}));

  // Removing a required field is rejected without changing either history.
  const demi::editor::SceneValueTarget requiredValues{
      .entityId = "ent_player", .component = "GameplayData", .field = "values"};
  const std::string beforeRequiredRemoval = document.json().dump();
  const bool couldUndoBeforeRequiredRemoval = document.canUndo();
  assert(!document.removeValue(requiredValues, error));
  assert(document.json().dump() == beforeRequiredRemoval);
  assert(document.canUndo() == couldUndoBeforeRequiredRemoval);
  assert(document.issueFor(requiredValues) != nullptr);

  const std::string beforeRejectedEdit = document.json().dump();
  const bool couldUndoBeforeRejectedEdit = document.canUndo();
  assert(!document.setValue(position, "not-a-vector", false, error));
  assert(!error.empty());
  assert(document.json().dump() == beforeRejectedEdit);
  assert(document.canUndo() == couldUndoBeforeRejectedEdit);
  assert(document.issueFor(position) != nullptr);

  // A multi-edit is one command: every target commits or none do, and one
  // undo restores the exact pre-edit document.
  const demi::editor::SceneValueTarget scale{.entityId = "ent_player",
                                             .component = "Transform3D",
                                             .field = "scale"};
  const std::string beforeMultiEdit = document.json().dump();
  assert(document.setValues({position, scale}, {4.0, 4.0, 4.0}, error));
  assert(document.component("ent_player", "Transform3D")->at("position") ==
         nlohmann::json({4.0, 4.0, 4.0}));
  assert(document.component("ent_player", "Transform3D")->at("scale") ==
         nlohmann::json({4.0, 4.0, 4.0}));
  assert(document.undo(error));
  assert(document.json().dump() == beforeMultiEdit);
  const std::string beforeRejectedMultiEdit = document.json().dump();
  assert(!document.setValues({position, requiredValues},
                             {6.0, 6.0, 6.0}, error));
  assert(document.json().dump() == beforeRejectedMultiEdit);

  error.clear();
  assert(document.save(error));
  assert(!document.isDirty());
  assert(document.undo(error));
  assert(document.isDirty());
  assert(document.redo(error));
  assert(!document.isDirty());

  // A stale temporary file is replaced by the next successful atomic save.
  assert(document.setValue(position, {3.0, 4.0, 5.0}, false, error));
  const fs::path temporary = scene.string() + ".demi-editor.tmp";
  write(temporary, "stale temporary data");
  assert(document.save(error));
  assert(!fs::exists(temporary));

  // Each conflict outcome preserves the external file and in-memory edits.
  assert(document.setValue(position, {5.0, 6.0, 7.0}, false, error));
  const std::string external =
      "{\n  \"format_version\": 1, \"entities\": []\n}\n";
  write(scene, external);
  assert(!document.save(error));
  assert(error.find("changed on disk") != std::string::npos);
  assert(document.hasExternalConflict());
  assert(document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::KeepEditing, {}, error));
  assert(document.isDirty());
  assert((*document.component("ent_player", "Transform3D"))["position"] ==
         nlohmann::json({5.0, 6.0, 7.0}));

  assert(!document.save(error));
  assert(document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::Cancel, {}, error));
  assert(document.isDirty());

  assert(!document.save(error));
  assert(!document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::SaveCopy, scene, error));
  assert(document.hasExternalConflict());
  const fs::path occupiedCopy = root / "occupied.scene.json";
  write(occupiedCopy, "do not overwrite");
  assert(!document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::SaveCopy, occupiedCopy, error));
  assert(document.hasExternalConflict());
  std::ifstream occupiedInput(occupiedCopy);
  std::string occupiedText;
  std::getline(occupiedInput, occupiedText);
  assert(occupiedText == "do not overwrite");
  const fs::path copy = root / "main.copy.scene.json";
  assert(document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::SaveCopy, copy, error));
  demi::editor::EditorSceneDocument copied;
  assert(copied.open(copy, error));
  assert(copied.component("ent_player", "Transform3D")->at("position") ==
         nlohmann::json({5.0, 6.0, 7.0}));
  assert(document.isDirty());

  assert(!document.save(error));
  assert(document.resolveExternalChange(
      demi::editor::ExternalChangeDecision::ReloadFromDisk, {}, error));
  assert(!document.isDirty());
  assert(!document.canUndo());
  assert(!document.canRedo());
  assert(document.json().at("entities").empty());

  // Missing/deleted files become explicit failures or conflicts.
  demi::editor::EditorSceneDocument missing;
  assert(!missing.open(root / "missing.scene.json", error));
  assert(copied.setValue(position, {8.0, 9.0, 10.0}, false, error));
  assert(fs::remove(copy));
  assert(!copied.save(error));
  assert(copied.hasExternalConflict());

#if defined(__unix__)
  // A directory without owner write permission cannot accept the atomic temp
  // file. Root bypasses this restriction, so that environment skips the check.
  if (geteuid() != 0) {
    const fs::path protectedDirectory = root / "read_only";
    fs::create_directories(protectedDirectory);
    const fs::path protectedScene = protectedDirectory / "main.scene.json";
    write(protectedScene, kScene);
    demi::editor::EditorSceneDocument protectedDocument;
    assert(protectedDocument.open(protectedScene, error));
    assert(protectedDocument.setValue(position, {4.0, 4.0, 4.0}, false, error));
    fs::permissions(protectedDirectory,
                    fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    assert(!protectedDocument.save(error));
    assert(!fs::exists(protectedScene.string() + ".demi-editor.tmp"));
    fs::permissions(protectedDirectory, fs::perms::owner_all,
                    fs::perm_options::replace);
  }
#endif

  fs::remove_all(root, ignored);
  return 0;
}
