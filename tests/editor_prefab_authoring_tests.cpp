#include "editor/EditorPrefabAuthoring.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorSceneJson.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  assert(output.good());
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  using demi::editor::EditorSceneDocument;
  using demi::editor::SceneValueTarget;
  using nlohmann::json;

  const fs::path root =
      fs::temp_directory_path() / "demi_editor_prefab_authoring_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  write(root / "demi.project.json",
        R"({"format_version":1,"name":"Editor prefab authoring"})");
  write(root / "prefabs/crate.prefab.json", R"({
    "format_version": 1,
    "id": "prefab://crate",
    "entities": [{
      "id": "root",
      "components": {"Transform3D": {"position": [0, 0, 0]}}
    }]
  })");
  const fs::path scenePath = root / "scenes/main.scene.json";
  write(scenePath, R"({
    "format_version": 1,
    "id": "scene://main",
    "entities": [{
      "id": "crate",
      "components": {"Transform3D": {}}
    }],
    "instances": [{"id": "crate_2", "prefab": "prefab://crate"}]
  })");

  EditorSceneDocument document;
  std::string error;
  assert(document.open(scenePath, error));
  assert(document.instantiatePrefab("prefab://crate", error));
  const std::string instanceId(document.lastChangedEntityId());
  assert(instanceId == "crate_3");
  const json *instance = document.entity(instanceId);
  assert(instance != nullptr);
  assert(*instance == json({{"id", instanceId},
                            {"prefab", "prefab://crate"}}));

  const SceneValueTarget position{
      .entityId = instanceId + "/root",
      .component = "Transform3D",
      .field = "position",
      .prefabInstanceId = instanceId,
      .prefabEntityId = "root"};
  assert(document.setValue(position, {4.0, 5.0, 6.0}, false, error));
  assert(document.entity(instanceId)
             ->at("overrides")
             .at("root")
             .at("components")
             .at("Transform3D")
             .at("position") == json({4.0, 5.0, 6.0}));
  assert(document.undo(error));
  assert(!document.entity(instanceId)->contains("overrides"));
  assert(document.undo(error));
  assert(document.entity(instanceId) == nullptr);
  assert(document.redo(error));
  assert(document.lastChangedEntityId() == instanceId);
  assert(document.redo(error));
  assert(document.entity(instanceId)->contains("overrides"));
  assert(document.save(error));

  // A positioned instance is authored as one insertion command, including its
  // initial transform override. One Undo therefore removes the whole drop.
  const json beforePositionedInstance = document.json();
  json placement = json::object();
  placement["root"]["components"]["Transform3D"]["position"] =
      {8.0, 1.0, -3.0};
  assert(document.instantiatePrefab("prefab://crate", placement, error));
  const std::string positionedId(document.lastChangedEntityId());
  assert(document.entity(positionedId)
             ->at("overrides")
             .at("root")
             .at("components")
             .at("Transform3D")
             .at("position") == json({8.0, 1.0, -3.0}));
  assert(document.undo(error));
  assert(document.json() == beforePositionedInstance);
  assert(document.redo(error));
  assert(document.entity(positionedId) != nullptr);
  assert(document.undo(error));

  EditorSceneDocument reopened;
  assert(reopened.open(scenePath, error));
  assert(reopened.entity(instanceId) != nullptr);
  const json *reopenedPosition =
      demi::editor::valueInDocument(reopened.json(), position);
  assert(reopenedPosition != nullptr);
  assert(*reopenedPosition == json({4.0, 5.0, 6.0}));
  const std::string beforeInvalidReference = reopened.json().dump();
  assert(!reopened.instantiatePrefab("prefab://missing", error));
  assert(!error.empty());
  assert(reopened.json().dump() == beforeInvalidReference);
  assert(!reopened.canUndo());

  // Override lookup reaches inline prefab entities nested under authored
  // children while preserving the supported legacy instances source shape.
  json mixedSource = json::parse(R"({
    "entities": [{
      "id": "group",
      "components": {"Transform3D": {}},
      "children": [{
        "id": "nested",
        "prefab": "prefab://crate",
        "overrides": {"root.Transform3D.position": [1, 2, 3]}
      }]
    }],
    "instances": [{
      "id": "legacy",
      "prefab": "prefab://crate",
      "overrides": {
        "root": {
          "components": {"Transform3D": {"position": [7, 8, 9]}}
        }
      }
    }]
  })");
  const SceneValueTarget nestedPosition{
      .entityId = "nested/root",
      .component = "Transform3D",
      .field = "position",
      .prefabInstanceId = "nested",
      .prefabEntityId = "root"};
  assert(*demi::editor::valueInDocument(mixedSource, nestedPosition) ==
         json({1, 2, 3}));
  assert(demi::editor::assignValueInDocument(mixedSource, nestedPosition,
                                             json({3, 2, 1})));
  assert(mixedSource["entities"][0]["children"][0]["overrides"]
                    ["root.Transform3D.position"] == json({3, 2, 1}));
  const SceneValueTarget legacyPosition{
      .entityId = "legacy/root",
      .component = "Transform3D",
      .field = "position",
      .prefabInstanceId = "legacy",
      .prefabEntityId = "root"};
  assert(*demi::editor::valueInDocument(mixedSource, legacyPosition) ==
         json({7, 8, 9}));

  // Export copies the authored source shape and stable IDs. Only the selected
  // root's external transform parent is detached; local pose and other
  // references remain authored exactly as they were.
  const json exportScene = json::parse(R"({
    "format_version": 1,
    "id": "scene://export",
    "entities": [{
      "id": "external",
      "components": {"Transform3D": {}},
      "children": [{
        "id": "selected",
        "components": {
          "Transform3D": {
            "parent": "external",
            "position": [2, 3, 4],
            "rotation": [0, 0.5, 0]
          },
          "GameplayData": {"values": {"external_target": "external"}}
        },
        "children": [{
          "id": "nested_child",
          "components": {"Transform3D": {"position": [0, 1, 0]}}
        }]
      }]
    }, {
      "id": "flat_child",
      "components": {"Transform3D": {"parent": "selected"}}
    }, {
      "id": "placed",
      "prefab": "prefab://crate",
      "overrides": {"root.Transform3D.position": [9, 8, 7]}
    }]
  })");
  const json unchangedExportScene = exportScene;
  const auto exported = demi::editor::makeEntityPrefab(
      exportScene, "selected", "prefab://selection", error);
  assert(exported.has_value());
  assert(error.empty());
  assert(exported->at("format_version") == 1);
  assert(exported->at("id") == "prefab://selection");
  assert(exported->at("entities").size() == 2);
  const json &exportedRoot = exported->at("entities")[0];
  assert(exportedRoot.at("id") == "selected");
  assert(!exportedRoot.at("components").at("Transform3D").contains("parent"));
  assert(exportedRoot.at("components").at("Transform3D").at("position") ==
         json({2, 3, 4}));
  assert(exportedRoot.at("components").at("Transform3D").at("rotation") ==
         json({0, 0.5, 0}));
  assert(exportedRoot.at("components")
             .at("GameplayData")
             .at("values")
             .at("external_target") == "external");
  assert(exportedRoot.at("children")[0].at("id") == "nested_child");
  assert(exported->at("entities")[1].at("id") == "flat_child");
  assert(exported->at("entities")[1]
             .at("components")
             .at("Transform3D")
             .at("parent") == "selected");
  assert(exportScene == unchangedExportScene);

  // An expanded child can export only as a copy of its resolvable authored
  // whole instance. Unknown expanded content is never silently baked.
  const auto copiedInstance = demi::editor::makeEntityPrefab(
      exportScene, "placed/root", "prefab://placed_copy", error);
  assert(copiedInstance.has_value());
  assert(copiedInstance->at("entities").size() == 1);
  assert(copiedInstance->at("entities")[0] == exportScene["entities"][2]);
  const auto rejectedExpanded = demi::editor::makeEntityPrefab(
      exportScene, "unknown/root", "prefab://bad_copy", error);
  assert(!rejectedExpanded.has_value());
  assert(error.find("cannot be baked or unpacked") != std::string::npos);

  const json nestedScene = {
      {"format_version", 1}, {"id", "scene://nested"},
      {"entities", json::array({{
          {"id", "group"}, {"components", {{"Transform3D", json::object()}}},
          {"children", json::array({{{"id", "nested"}, {"prefab", "prefab://crate"}}})}
      }})}};
  const auto nested = demi::runtime::composition::expandScene(scenePath, nestedScene, false);
  assert(nested.document);
  const auto &nestedRoot = nested.document->at("entities")[1];
  assert(nestedRoot.at("id") == "nested/root");
  assert(nestedRoot.at("components").at("Transform3D").at("parent") == "group");
  const auto origin = demi::runtime::composition::prefabEntityOrigin(nestedScene, "nested/root");
  assert(origin && origin->instanceId == "nested" && origin->localEntityId == "root");

  EditorSceneDocument prefabDocument;
  assert(prefabDocument.open(root / "prefabs/crate.prefab.json", error));
  const auto beforeSelf = prefabDocument.json();
  assert(!prefabDocument.instantiatePrefab("prefab://crate", error));
  assert(prefabDocument.json() == beforeSelf && !prefabDocument.canUndo());

  const auto beforeInstanceCopy = reopened.json();
  assert(reopened.duplicateEntity("crate_2", error));
  const auto copiedId = std::string(reopened.lastChangedEntityId());
  assert(copiedId == "crate_2_copy");
  assert(reopened.json().at("instances").size() == 2);
  assert(reopened.undo(error));
  assert(reopened.json() == beforeInstanceCopy);
  assert(reopened.redo(error));
  assert(reopened.deleteEntities(std::vector<std::string>{"crate", copiedId}, error));
  assert(reopened.entity("crate") == nullptr);
  assert(reopened.json().at("instances").size() == 1);
  assert(reopened.undo(error));
  assert(reopened.entity("crate") != nullptr);
  assert(reopened.json().at("instances").size() == 2);

  fs::remove_all(root, ignored);
  return 0;
}
