#include "editor/EditorAuthoringClipboard.h"
#include "editor/EditorClipboard.h"
#include "editor/EditorHudDocument.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorWorkspace.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
using Json = nlohmann::json;
using namespace demi::editor;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void write(const std::filesystem::path &path, const Json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2);
  require(output.good(), "Failed to write clipboard test fixture");
}

Json entity(const std::string &id) {
  return {{"id", id}, {"components", {{"Transform2D", Json::object()}}}};
}

void sceneClipboard(const std::filesystem::path &root) {
  auto group = entity("group");
  auto child = entity("child");
  child["components"]["Joint2D"] = {{"type", "weld"},
                                    {"other_entity", "group"}};
  child["components"]["GameplayData"] = {{"values", {{"text", "group"}}}};
  group["children"] = Json::array({child});
  Json source{{"format_version", 1},
              {"id", "scene://clipboard/main"},
              {"entities",
               Json::array({group, entity("other"), entity("group_copy")})}};
  const auto scenePath = root / "scenes/main.scene.json";
  write(scenePath, source);
  EditorSceneDocument document;
  std::string error;
  require(document.open(scenePath, error), error);
  const std::vector<std::string> selection{"child", "group", "other"};
  auto payload = document.exportEntities(selection, error);
  require(payload && payload->size() == 2, "Nested child was copied twice");
  require((*payload)[0]["children"][0] == child,
          "Copy changed authored nesting");
  const auto envelope =
      encodeEditorClipboard(EditorClipboardKind::Entities, *payload);
  auto decoded = decodeEditorClipboard(envelope, error);
  require(decoded && decoded->data == *payload,
          "Scene clipboard envelope did not round-trip");
  std::vector<std::string> created;
  require(document.pasteEntities(decoded->data, created, error), error);
  require(created == std::vector<std::string>{"group_copy_2", "other_copy"},
          "Paste did not reserve existing IDs or preserve source order");
  require(document.entity("group_copy_2")->at("children")[0]["id"] ==
              "child_copy",
          "Paste flattened the nested source hierarchy");
  require(document.component("child_copy", "Joint2D")->at("other_entity") ==
              "group_copy_2",
          "Native component entity reference was not remapped");
  require(
      document.component("child_copy", "GameplayData")->at("values")["text"] ==
          "group",
      "Clipboard rewrote an untyped gameplay string");
  const auto pasted = document.json();
  require(document.undo(error) && document.json() == source,
          "Paste was not one atomic undo");
  require(document.redo(error) && document.json() == pasted,
          "Paste redo changed IDs");
  require(document.undo(error), error);
  require(document.duplicateEntities(std::vector<std::string>{"child"}, created,
                                     error),
          error);
  require(document.entity("group")->at("children").size() == 2,
          "Duplicating a nested child did not keep its source parent");
  require(document.undo(error) && document.json() == source,
          "Nested duplicate undo failed");

  auto malformed = *payload;
  malformed[0]["children"][0]["id"] = malformed[0]["id"];
  require(!document.pasteEntities(malformed, created, error) &&
              document.json() == source,
          "Duplicate clipboard IDs changed the source");
  malformed = *payload;
  malformed[1]["components"]["Transform2D"]["position"] = "invalid";
  require(!document.pasteEntities(malformed, created, error) &&
              document.json() == source,
          "Invalid native component paste did not roll back");
  require(!document.canUndo() && document.canRedo(),
          "Rejected paste changed history");
  require(!decodeEditorClipboard("{broken", error),
          "Invalid clipboard JSON was accepted");
  require(!decodeEditorClipboard(
              R"({"format_version":1,"kind":"unknown","data":[]})", error),
          "Unknown clipboard kind was accepted");

  const auto prefabPath = root / "prefabs/copy.prefab.json";
  write(prefabPath, {{"format_version", 1},
                     {"id", "prefab://copy"},
                     {"entities", Json::array()}});
  EditorSceneDocument prefab;
  require(prefab.open(prefabPath, error), error);
  require(prefab.pasteEntities(*payload, created, error), error);
  require(prefab.entity("group_copy")->contains("children"),
          "Cross-document prefab paste lost nesting");
  require(prefab.undo(error) && prefab.json()["entities"].empty(),
          "Prefab paste undo failed");

  auto self = Json::array({entity("self")});
  self[0]["components"]["Joint2D"] = {{"type", "weld"},
                                      {"other_entity", "self"}};
  remapClipboardEntities(self, {{"self", "self_copy"}});
  require(self[0]["components"]["Joint2D"]["other_entity"] == "self_copy",
          "Typed self reference was not remapped");
  self[0]["components"]["MeshRenderer"] = {{"model", "asset://self"}};
  self[0]["components"]["LuaScript"] = {{"module", "script://self"}};
  remapClipboardEntities(self, {{"self_copy", "another_copy"}});
  require(self[0]["components"]["MeshRenderer"]["model"] == "asset://self" &&
              self[0]["components"]["LuaScript"]["module"] == "script://self",
          "Typed resource URIs were rewritten by entity remapping");

  write(root / "demi.project.json", {{"format_version", 1}});
  auto body = entity("body");
  body["children"] = Json::array({entity("detail")});
  write(root / "prefabs/assembly.prefab.json",
        {{"format_version", 1},
         {"id", "prefab://assembly"},
         {"entities", Json::array({body})}});
  const Json instance{{"id", "prop"},
                      {"prefab", "prefab://assembly"},
                      {"overrides", {{"body.Transform2D.position", {4, 5}}}}};
  write(scenePath, {{"format_version", 1},
                    {"id", "scene://clipboard/main"},
                    {"entities", Json::array({instance})}});
  EditorSceneDocument instanceScene;
  require(instanceScene.open(scenePath, error), error);
  auto instancePayload =
      instanceScene.exportEntities(std::vector<std::string>{"prop"}, error);
  require(instancePayload && (*instancePayload)[0] == instance,
          "Scene prefab clipboard materialized or altered the source instance");
  require(instanceScene.duplicateEntities(std::vector<std::string>{"prop"},
                                          created, error),
          error);
  require(instanceScene.entity("prop_copy")->at("prefab") ==
                  "prefab://assembly" &&
              instanceScene.entity("prop_copy")->at("overrides") ==
                  instance["overrides"],
          "Scene prefab duplication lost its URI or overrides");
  require(instanceScene.undo(error) &&
              instanceScene.json()["entities"].size() == 1,
          "Scene prefab duplication did not undo atomically");
}

void hudClipboard(const std::filesystem::path &root) {
  const Json panel{
      {"id", "panel"},
      {"type", "panel"},
      {"children",
       Json::array({{{"id", "label"}, {"type", "label"}, {"text", "panel"}}})}};
  const Json button{{"id", "button"}, {"type", "button"}, {"text", "Action"}};
  const Json source{{"format_version", 1},
                    {"canvas_size", {800, 600}},
                    {"children", Json::array({panel, button})}};
  const auto hudPath = root / "scenes/main.hud.json";
  write(hudPath, source);
  EditorHudDocument hud;
  std::string error;
  require(hud.open(hudPath, error), error);
  auto payload = hud.exportNodes(
      std::vector<std::string>{"label", "panel", "button"}, error);
  require(payload && payload->size() == 2,
          "HUD multiselection did not filter selected descendants");
  std::vector<std::string> created;
  require(
      hud.duplicateNodes(std::vector<std::string>{"label", "panel", "button"},
                         created, error),
      error);
  require(created == std::vector<std::string>{"panel_copy", "button_copy"},
          "HUD multi-duplicate created the wrong roots");
  require(hud.authoredNode("label_copy") &&
              hud.authoredNode("label_copy")->at("text") == "panel",
          "HUD duplicated text was rewritten as an ID");
  const auto duplicated = hud.json();
  require(hud.undo(error) && hud.json() == source,
          "HUD multi-duplicate required multiple undos");
  require(hud.redo(error) && hud.json() == duplicated,
          "HUD multi-duplicate redo failed");
  require(hud.undo(error), error);
  require(hud.deleteNodes(std::vector<std::string>{"panel", "label", "button"},
                          error),
          error);
  require(hud.json()["children"].empty(),
          "HUD multi-delete did not remove all selected roots");
  require(hud.undo(error) && hud.json() == source,
          "HUD multi-delete did not undo atomically");
  require(hud.pasteNodes(*payload, "panel", created, error), error);
  require(hud.authoredNode("panel")->at("children").size() == 3,
          "HUD paste did not use the selected container");
  require(hud.undo(error) && hud.json() == source,
          "HUD multi-paste undo failed");
  require(!hud.exportNodes(std::vector<std::string>{"ui_root"}, error),
          "HUD root was copyable");
  require(
      !hud.deleteNodes(std::vector<std::string>{"panel", "missing"}, error) &&
          hud.json() == source,
      "Invalid HUD multi-delete was partially applied");
  auto malformed = *payload;
  malformed[1]["children"] = false;
  require(!hud.pasteNodes(malformed, {}, created, error) &&
              hud.json() == source,
          "Malformed HUD clipboard changed source");

  const auto uiPrefabPath = root / "ui/reusable.ui.prefab.json";
  write(
      uiPrefabPath,
      {{"format_version", 1},
       {"id", "ui-prefab://reusable"},
       {"root",
        {{"id", "root"}, {"type", "container"}, {"children", Json::array()}}}});
  EditorHudDocument prefab;
  require(prefab.open(uiPrefabPath, error), error);
  const auto before = prefab.json();
  require(prefab.pasteNodes(*payload, "root", created, error), error);
  require(prefab.authoredNode("panel_copy") &&
              prefab.authoredNode("label_copy"),
          "HUD-to-UI-prefab paste failed");
  require(prefab.undo(error) && prefab.json() == before,
          "UI prefab paste undo failed");

  write(root / "demi.project.json", {{"format_version", 1}});
  Json instanceSource = source;
  instanceSource["children"].push_back(
      {{"id", "reused"}, {"prefab", "ui-prefab://reusable"}});
  write(root / "ui/reusable.ui.prefab.json",
        {{"format_version", 1},
         {"id", "ui-prefab://reusable"},
         {"root",
          {{"id", "root"},
           {"type", "container"},
           {"children",
            Json::array({{{"id", "generated"}, {"type", "label"}}})}}}});
  write(hudPath, instanceSource);
  EditorHudDocument instanceHud;
  require(instanceHud.open(hudPath, error), error);
  auto instancePayload =
      instanceHud.exportNodes(std::vector<std::string>{"reused"}, error);
  require(instancePayload && instancePayload->size() == 1 &&
              (*instancePayload)[0]["prefab"] == "ui-prefab://reusable" &&
              !(*instancePayload)[0].contains("children"),
          "UI prefab clipboard materialized generated controls");
  require(instanceHud.duplicateNodes(std::vector<std::string>{"reused"},
                                     created, error),
          error);
  require(instanceHud.authoredNode("reused_copy")->at("prefab") ==
              "ui-prefab://reusable",
          "UI prefab duplication lost its resource URI");
  require(!instanceHud.exportNodes(std::vector<std::string>{"reused.generated"},
                                   error),
          "Generated UI prefab child was copyable");
}

void workspaceClipboard(const std::filesystem::path &root) {
  const auto directory = root / "workspace";
  write(directory / "scenes/main.scene.json",
        {{"format_version", 1},
         {"id", "scene://clipboard/main"},
         {"entities", Json::array({entity("actor"), entity("other")})}});
  write(directory / "demi.project.json",
        {{"format_version", 1},
         {"name", "Clipboard test"},
         {"main_scene", "scene://clipboard/main"},
         {"scenes", Json::array({{{"id", "scene://clipboard/main"},
                                  {"path", "scenes/main.scene.json"}}})}});
  write(directory / "scenes/main.hud.json",
        {{"format_version", 1},
         {"children", Json::array({{{"id", "one"}, {"type", "button"}},
                                   {{"id", "two"}, {"type", "label"}}})}});
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(directory, error), error);
  require(workspace.selectAllAuthored(error), error);
  require(workspace.selectedEntityIds().size() == 2,
          "Scene Select All was not multi-selection");
  auto copied = workspace.exportSelection(error);
  require(copied && workspace.pasteSelection(*copied, error), error);
  require(workspace.selectedEntityIds().size() == 2,
          "Scene paste lost multiple selected roots");
  require(workspace.undo(error), error);
  require(workspace.openHudDocument(directory / "scenes/main.hud.json", error),
          error);
  require(workspace.selectAllAuthored(error), error);
  require(workspace.selectedHudNodeIds() ==
              std::vector<std::string>{"one", "two"},
          "HUD Select All did not select genuine authored controls");
  copied = workspace.exportSelection(error);
  require(copied && workspace.duplicateSelection(error), error);
  require(workspace.selectedHudNodeIds().size() == 2,
          "HUD duplicate lost multi-selection");
  require(workspace.undo(error), error);
  const auto before = workspace.hudDocument()->json();
  require(!workspace.pasteSelection(
              encodeEditorClipboard(EditorClipboardKind::Entities,
                                    Json::array({entity("wrong")})),
              error) &&
              workspace.hudDocument()->json() == before,
          "A scene clipboard was pasted into a HUD");
  require(workspace.selectAllAuthored(error) &&
              workspace.deleteSelection(error),
          error);
  require(workspace.hudDocument()->json()["children"].empty(),
          "HUD Select All/Delete left controls behind");
  require(workspace.undo(error) && workspace.hudDocument()->json() == before,
          "HUD Select All/Delete required multiple undos");
  workspace.selectHudNode("one");
  workspace.toggleHudNodeSelection("two");
  require(workspace.selectedHudNodeIds().size() == 2 &&
              workspace.isHudNodeSelected("one"),
          "HUD Ctrl-click selection lost the primary node");

  auto body = entity("body");
  body["children"] = Json::array({entity("detail")});
  write(directory / "prefabs/assembly.prefab.json",
        {{"format_version", 1},
         {"id", "prefab://assembly"},
         {"entities", Json::array({body})}});
  write(directory / "scenes/main.scene.json",
        {{"format_version", 1},
         {"id", "scene://clipboard/main"},
         {"entities",
          Json::array({{{"id", "prop"}, {"prefab", "prefab://assembly"}}})}});
  require(workspace.refresh(error),
          "External scene changes were not refreshed: " + error);
  require(
      workspace.openSceneDocument(directory / "scenes/main.scene.json", error),
      error);
  workspace.selectEntity("prop/detail");
  require(!workspace.exportSelection(error),
          "Generated scene prefab child was copyable");
  const auto instanceBefore = workspace.sceneDocument().json();
  require(!workspace.deleteSelection(error) &&
              workspace.sceneDocument().json() == instanceBefore,
          "Generated scene prefab child deletion altered its instance");
  workspace.selectEntity("prop/body");
  copied = workspace.exportSelection(error);
  require(copied && workspace.duplicateSelection(error), error);
  require(workspace.sceneDocument().entity("prop_copy")->at("prefab") ==
              "prefab://assembly",
          "Expanded scene prefab root did not copy the authored instance "
          "reference");
  require(workspace.undo(error) &&
              workspace.sceneDocument().json() == instanceBefore,
          "Expanded prefab root duplication did not undo atomically");
}

} // namespace

int main() {
  char fixture[] = "/tmp/demi-authoring-clipboard-XXXXXX";
  const char *created = mkdtemp(fixture);
  if (!created) {
    std::cerr << "Could not create clipboard fixture directory\n";
    return 1;
  }
  const std::filesystem::path root(created);
  try {
    sceneClipboard(root);
    hudClipboard(root);
    workspaceClipboard(root);
    std::filesystem::remove_all(root);
    std::cout << "Authoring clipboard tests passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << "\nFixture retained at " << root << '\n';
    return 1;
  }
}
