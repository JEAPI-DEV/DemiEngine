#include "editor/EditorSceneCommand.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorSceneJson.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char *kScene = R"({
  "format_version": 1,
  "id": "scene://test/main",
  "name": "Main",
  "entities": [
    {
      "id": "root",
      "name": "Root",
      "components": {
        "Transform3D": { "position": [0.0, 0.0, 0.0] }
      }
    },
    {
      "id": "child",
      "name": "Child",
      "components": {
        "Transform3D": { "parent": "root", "position": [1.0, 1.0, 1.0] }
      }
    }
  ]
})";

void write(const std::filesystem::path &path, const std::string &text) {
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

std::filesystem::path makeFixture() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "demi_editor_scene_commands";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const std::filesystem::path scene = root / "main.scene.json";
  write(scene, kScene);
  return scene;
}

bool entityExists(const demi::editor::EditorSceneDocument &document,
                  const std::string &id) {
  return document.entity(id) != nullptr;
}

} // namespace

int main() {
  using demi::editor::EditorSceneDocument;

  const std::filesystem::path scene = makeFixture();
  EditorSceneDocument document;
  std::string error;

  // Reusable scene-JSON helpers stay independent of document state.
  const nlohmann::json parsed = nlohmann::json::parse(kScene);
  assert(demi::editor::collectSubtreeIds(parsed, "root") ==
         std::vector<std::string>({"root", "child"}));
  assert(demi::editor::collectSubtreeIds(parsed, "child") ==
         std::vector<std::string>({"child"}));
  assert(demi::editor::transformComponentName(parsed["entities"][1]) ==
         std::string_view("Transform3D"));
  const nlohmann::json isoEntity = {
      {"components", {{"IsoTransform", {{"parent", "road_root"}}}}}};
  assert(demi::editor::transformComponentName(isoEntity) ==
         std::string_view("IsoTransform"));
  assert(demi::editor::transformParentId(isoEntity) == "road_root");
  assert(demi::editor::uniqueEntityId(parsed, "root") == "root_2");
  nlohmann::json cyclic = parsed;
  cyclic["entities"][0]["components"]["Transform3D"]["parent"] = "child";
  assert(demi::editor::collectSubtreeIds(cyclic, "root").size() == 2);

  assert(document.open(scene, error));

  // Create: unique id, undo/redo round-trip.
  assert(document.createEntity(error));
  const std::string createdId(document.lastChangedEntityId());
  assert(createdId == "ent_new");
  assert(entityExists(document, createdId));
  const nlohmann::json *created = document.entity(createdId);
  assert(created->value("name", "") == "New Entity");
  assert(created->at("components").is_object());
  assert(document.undo(error));
  assert(!entityExists(document, createdId));
  assert(document.redo(error));
  assert(entityExists(document, createdId));

  // Delete: undo restores the full authored JSON.
  assert(document.deleteEntity("child", error));
  assert(!entityExists(document, "child"));
  assert(document.undo(error));
  const nlohmann::json *restored = document.entity("child");
  assert(restored != nullptr);
  assert(restored->at("components").at("Transform3D").at("parent") == "root");
  assert(restored->at("components").at("Transform3D").at("position") ==
         nlohmann::json({1.0, 1.0, 1.0}));

  // Deleting a parent removes its complete subtree as one reversible command.
  const std::string beforeSubtreeDelete = document.json().dump();
  assert(document.deleteEntity("root", error));
  assert(!entityExists(document, "root"));
  assert(!entityExists(document, "child"));
  assert(document.undo(error));
  assert(document.json().dump() == beforeSubtreeDelete);

  // Duplicate: subtree remap preserves internal hierarchy, undo removes copies.
  assert(document.duplicateEntity("root", error));
  const std::string copyId(document.lastChangedEntityId());
  assert(copyId == "root_copy");
  assert(entityExists(document, "root_copy"));
  assert(entityExists(document, "child_copy"));
  const nlohmann::json *copy = document.entity("child_copy");
  assert(copy->at("components").at("Transform3D").at("parent") == "root_copy");
  assert(document.undo(error));
  assert(!entityExists(document, "root_copy"));
  assert(!entityExists(document, "child_copy"));
  assert(document.redo(error));
  assert(entityExists(document, "root_copy"));

  // Reparent: cycles and missing parents are rejected without changing state.
  const bool hadUndo = document.canUndo();
  assert(!document.reparent("root", "child", error));
  assert(!error.empty());
  assert(document.canUndo() == hadUndo);
  assert(!document.reparent("child", "missing", error));
  assert(!error.empty());

  // Direct inspector edits use the same full-document hierarchy validation.
  assert(!document.setValue(
      {.entityId = "child", .component = "Transform3D", .field = "parent"},
      "missing", false, error));
  assert(document.entity("child")
             ->at("components")
             .at("Transform3D")
             .at("parent") == "root");

  // Reparent to root removes the authored parent field and undoes exactly.
  assert(document.reparent("child", std::nullopt, error));
  const nlohmann::json *rooted = document.entity("child");
  assert(rooted->at("components").at("Transform3D").find("parent") ==
         rooted->at("components").at("Transform3D").end());
  assert(document.undo(error));
  assert(document.entity("child")
             ->at("components")
             .at("Transform3D")
             .at("parent") == "root");

  // Add component: canonical defaults, duplicate and unknown rejection.
  assert(document.addComponent("root", "Transform2D", error));
  assert(document.component("root", "Transform2D") != nullptr);
  assert(document.undo(error));
  assert(document.component("root", "Transform2D") == nullptr);
  assert(!document.addComponent("root", "Transform3D", error));
  assert(!error.empty());
  assert(!document.addComponent("root", "NotAComponent", error));
  assert(!error.empty());

  // Remove component: preserves the full JSON for undo.
  assert(document.removeComponent("child", "Transform3D", error));
  assert(document.component("child", "Transform3D") == nullptr);
  assert(document.undo(error));
  const nlohmann::json *restoredTransform =
      document.component("child", "Transform3D");
  assert(restoredTransform != nullptr);
  assert(restoredTransform->at("position") == nlohmann::json({1.0, 1.0, 1.0}));

  // Removing a parent's transform would orphan its child, so the staged
  // document is rejected and nothing changes.
  assert(!document.removeComponent("root", "Transform3D", error));
  assert(!error.empty());
  assert(document.component("root", "Transform3D") != nullptr);

  std::error_code ignored;
  std::filesystem::remove_all(scene.parent_path(), ignored);
  return 0;
}
