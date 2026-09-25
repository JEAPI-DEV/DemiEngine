#include "editor/EditorHudCanvas.h"
#include "editor/EditorHudDocument.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
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
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "demi-editor-hud-document";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  fs::create_directories(root / "ui");
  {
    std::ofstream output(root / "demi.project.json");
    output << R"({"format_version":1})";
  }
  {
    std::ofstream output(root / "ui/card.ui.prefab.json");
    output
        << R"({"format_version":1,"id":"ui-prefab://card","parameters":{"title":{"type":"string"},"count":{"type":"integer","default":3}},"root":{"id":"card","type":"panel","children":[{"id":"title","type":"label","text":"${title}"}]}})";
  }
  const fs::path path = root / "main.hud.json";
  {
    std::ofstream output(path);
    output
        << R"({"format_version":1,"canvas_size":[320,180],"root":{"id":"root","type":"container","anchor_min":[0,0],"anchor_max":[1,1],"children":[]}})";
  }

  std::string error;
  demi::editor::EditorHudDocument document;
  assert(document.open(path, error));
  std::string created;
  assert(document.createNode("button", "root", created, error));
  assert(created == "button");
  assert(document.preview().nodes.size() == 2);
  assert(document.preview().nodes[1].parent == "root");
  assert(demi::editor::pickEditorHudNode(document.preview(), {30, 30})->id ==
         "button");
  assert(demi::editor::pickEditorHudNode(document.preview(), {300, 160}) ==
         nullptr);
  assert(document.setNodeField("button", "position", {40, 50}, error));
  assert(document.preview().nodes[1].resolved.x == 40.0F);
  assert(document.undo(error));
  assert(document.preview().nodes[1].resolved.x == 24.0F);
  assert(document.redo(error));
  assert(document.setNodeField("button", "position",
                               {28.668174743652344, 55.96154022216797}, error));
  assert(document.authoredNode("button")->at("position") ==
         nlohmann::json({28.7, 56.0}));
  assert(document.preview().nodes[1].resolved.x == 28.7F);
  assert(document.preview().nodes[1].resolved.y == 56.0F);
  assert(document.setNodeField(
      "button", "anchor_min", {0.3333333432674408, 0.6666666865348816}, error));
  assert(document.authoredNode("button")->at("anchor_min") ==
         nlohmann::json({0.33, 0.67}));
  const nlohmann::json beforeDock = document.json();
  assert(document.setNodeField("button", "dock", "fill", error));
  assert(document.authoredNode("button")->at("dock") == "fill");
  assert(!document.authoredNode("button")->contains("anchor_min"));
  assert(!document.authoredNode("button")->contains("anchor_max"));
  assert(document.undo(error));
  assert(document.json() == beforeDock);
  assert(document.setNodeField("button", "dock", "fill", error));
  assert(document.setNodeField("button", "anchor_min", {0.25, 0.25}, error));
  assert(!document.authoredNode("button")->contains("dock"));
  assert(document.authoredNode("button")->at("anchor_max") ==
         nlohmann::json({1.0, 1.0}));
  assert(document.undo(error));
  assert(document.authoredNode("button")->at("dock") == "fill");
  assert(document.undo(error));
  assert(document.json() == beforeDock);
  assert(document.setNodeField("root", "layout", "row", error));
  assert(document.setNodeField("root", "stack", "column", error));
  assert(document.authoredNode("root")->at("stack") == "column");
  assert(!document.authoredNode("root")->contains("layout"));
  assert(document.undo(error));
  assert(document.authoredNode("root")->at("layout") == "row");
  assert(!document.authoredNode("root")->contains("stack"));
  assert(document.undo(error));
  assert(document.setNodeField("root", "padding", {1, 2, 3, 4}, error));
  assert(document.setNodeField("root", "pad", 8, error));
  assert(document.authoredNode("root")->at("pad") == 8);
  assert(!document.authoredNode("root")->contains("padding"));
  assert(document.undo(error));
  assert(document.authoredNode("root")->at("padding") ==
         nlohmann::json({1, 2, 3, 4}));
  assert(document.undo(error));
  assert(document.setNodeAnchors("button", {0.5F, 0.5F}, {0.5F, 0.5F}, error));
  assert(document.authoredNode("button")->at("anchor_min") ==
         nlohmann::json({0.5, 0.5}));
  assert(document.authoredNode("button")->at("anchor_max") ==
         nlohmann::json({0.5, 0.5}));
  assert(document.undo(error));
  assert(document.json() == beforeDock);
  std::string prefabId;
  assert(document.createPrefabInstance("ui-prefab://card", "root", prefabId,
                                       error));
  assert(prefabId == "card");
  const nlohmann::json *prefab = document.authoredNode(prefabId);
  assert(prefab != nullptr && prefab->at("prefab") == "ui-prefab://card");
  assert(prefab->at("arguments").at("title") == "");
  assert(prefab->at("arguments").at("count") == 3);
  assert(!document.setNodeField(prefabId, "visible", false, error));
  nlohmann::json arguments = prefab->at("arguments");
  arguments["title"] = "Card title";
  assert(document.setNodeField(prefabId, "arguments", arguments, error));
  const auto prefabTitle = std::ranges::find(
      document.preview().nodes, "card.title", &demi::runtime::ui::UiNode::id);
  assert(prefabTitle != document.preview().nodes.end());
  assert(prefabTitle->text == "Card title");
  assert(document.undo(error));
  assert(document.undo(error));
  assert(document.json() == beforeDock);
  assert(document.deleteNode("button", error));
  assert(document.preview().nodes.size() == 1);
  assert(!document.deleteNode("root", error));
  assert(document.undo(error));
  assert(document.preview().nodes.size() == 2);
  assert(document.setCanvasSize({640.04F, 359.96F}, error));
  assert(document.json().at("canvas_size") == nlohmann::json({640.0, 360.0}));
  assert(document.preview().canvasSize.x == 640.0F);
  assert(document.preview().canvasSize.y == 360.0F);
  assert(document.undo(error));
  assert(document.json().at("canvas_size") == nlohmann::json({320, 180}));
  assert(document.setCanvasSize({640.04F, 359.96F}, error));
  assert(document.save(error));
  const std::string saved = read(path);
  assert(saved.find("{\"format_version\":1,") == 0);
  const nlohmann::json savedDocument = nlohmann::json::parse(saved);
  assert(savedDocument["root"]["children"][0]["position"] ==
         nlohmann::json({28.7, 56.0}));
  assert(savedDocument["root"]["children"][0]["anchor_min"] ==
         nlohmann::json({0.33, 0.67}));
  assert(savedDocument["canvas_size"] == nlohmann::json({640.0, 360.0}));
  const auto implicit = root / "implicit.hud.json";
  {
    std::ofstream output(implicit);
    output
        << R"({"format_version":1,"canvas_size":[320,180],"children":[{"id":"label","type":"label","text":"Before","position":[4,8]}]})";
  }
  demi::editor::EditorHudDocument shorthand;
  assert(shorthand.open(implicit, error));
  assert(shorthand.authoredNode("label") && shorthand.authoredNode("ui_root"));
  assert(shorthand.setNodeField("label", "text", "After", error));
  assert(shorthand.setNodeField("label", "position", {30, 40}, error));
  assert(!shorthand.json().contains("root"));
  assert(shorthand.createNode("button", "ui_root", created, error));
  assert(shorthand.deleteNode(created, error));
  assert(shorthand.save(error));
  assert(shorthand.open(implicit, error));
  assert(shorthand.authoredNode("label")->at("text") == "After");
  assert(shorthand.hasImplicitRoot());
  assert(!shorthand.setNodeField("ui_root", "padding", {4, 4}, error));
  assert(!shorthand.json().contains("root"));
  assert(shorthand.createPrefabInstance("ui-prefab://card", "ui_root", prefabId,
                                        error));
  assert(!shorthand.json().contains("root"));
  assert(shorthand.undo(error));
  assert(!shorthand.json().contains("root"));

  const auto hierarchyPath = root / "hierarchy.hud.json";
  {
    std::ofstream output(hierarchyPath);
    output << R"({"format_version":1,"canvas_size":[320,180],"action_effects":{"show_external":{"show":["external"],"focus":"external"}},"root":{"id":"root","type":"container","children":[{"id":"group","type":"panel","text":"group","action":"external_callback","children":[{"id":"child","type":"button","text":"child","action":"external_callback","parent":"group"},{"id":"target","type":"label","text":"group child target"}]},{"id":"destination","type":"container"},{"id":"external","type":"label"},{"id":"card","prefab":"ui-prefab://card","arguments":{"title":"Card","count":3}}]}})";
  }
  demi::editor::EditorHudDocument hierarchy;
  assert(hierarchy.open(hierarchyPath, error));
  const nlohmann::json hierarchyBeforeDuplicate = hierarchy.json();
  std::string duplicateId;
  assert(hierarchy.duplicateNode("group", duplicateId, error));
  assert(duplicateId == "group_copy");
  const auto &rootChildren = hierarchy.json().at("root").at("children");
  assert(rootChildren.at(0).at("id") == "group");
  assert(rootChildren.at(1).at("id") == "group_copy");
  const nlohmann::json *duplicate = hierarchy.authoredNode(duplicateId);
  assert(duplicate != nullptr);
  assert(duplicate->at("text") == "group");
  assert(duplicate->at("action") == "external_callback");
  assert(duplicate->at("children").at(0).at("id") == "child_copy");
  assert(duplicate->at("children").at(0).at("parent") == "group_copy");
  assert(duplicate->at("children").at(0).at("text") == "child");
  assert(duplicate->at("children").at(0).at("action") ==
         "external_callback");
  assert(duplicate->at("children").at(1).at("id") == "target_copy");
  assert(duplicate->at("children").at(1).at("text") ==
         "group child target");
  assert(hierarchy.json().at("action_effects") ==
         hierarchyBeforeDuplicate.at("action_effects"));
  assert(hierarchy.undo(error));
  assert(hierarchy.json() == hierarchyBeforeDuplicate);

  assert(hierarchy.reparentNode("target", "destination", error));
  assert(hierarchy.authoredNode("target") != nullptr);
  assert(hierarchy.authoredNode("target")->at("text") ==
         "group child target");
  const auto movedPreview = std::ranges::find(
      hierarchy.preview().nodes, "target", &demi::runtime::ui::UiNode::id);
  assert(movedPreview != hierarchy.preview().nodes.end());
  assert(movedPreview->parent == "destination");
  assert(hierarchy.undo(error));
  const auto restoredPreview = std::ranges::find(
      hierarchy.preview().nodes, "target", &demi::runtime::ui::UiNode::id);
  assert(restoredPreview != hierarchy.preview().nodes.end());
  assert(restoredPreview->parent == "group");

  const nlohmann::json hierarchyBeforeFailures = hierarchy.json();
  assert(!hierarchy.reparentNode("root", "destination", error));
  assert(!hierarchy.reparentNode("group", "child", error));
  assert(!hierarchy.reparentNode("group", "card", error));
  assert(!hierarchy.reparentNode("card.title", "destination", error));
  assert(!hierarchy.duplicateNode("root", duplicateId, error));
  assert(!hierarchy.duplicateNode("card.title", duplicateId, error));
  assert(hierarchy.json() == hierarchyBeforeFailures);

  assert(!document.setCanvasSize({0.0F, 360.0F}, error));
  demi::editor::EditorHudDocument uiPrefab;
  assert(uiPrefab.open(root / "ui/card.ui.prefab.json", error));
  assert(!uiPrefab.setCanvasSize({640.0F, 360.0F}, error));
  fs::remove_all(root, ignored);
}
