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

void verifyPrefabInstanceOverrides(const std::filesystem::path &root) {
  using Json = nlohmann::json;
  std::filesystem::create_directories(root / "ui");
  const auto write = [](const auto &path, const std::string &text) {
    std::ofstream output(path);
    output << text;
  };
  write(root / "demi.project.json", R"({"format_version":1})");
  const std::string source = R"({"format_version":1,"id":"ui-prefab://card",
    "parameters":{"title":{"type":"string","default":"Shared"}},
    "root":{"id":"card","type":"panel","size":[300,160],"children":[
      {"id":"title","type":"label","text":"${title}","size":[120,30],"anchor_min":[0,0],"anchor_max":[0,0]},
      {"id":"nested","prefab":"ui-prefab://nested"}]}})";
  write(root / "ui/card.ui.prefab.json", source);
  write(root / "ui/nested.ui.prefab.json",
        R"({"format_version":1,"id":"ui-prefab://nested",
    "root":{"id":"inner","type":"container","size":[100,50],"children":[
      {"id":"caption","type":"label","text":"Nested"}]}})");
  const auto path = root / "test.hud.json";
  write(path, R"({"format_version":1,"canvas_size":[800,600],"children":[
    {"id":"alpha.dot","prefab":"ui-prefab://card"},
    {"id":"beta","prefab":"ui-prefab://card"},
    {"id":"alpha.dot.unrelated","type":"label","text":"Authored"}]})");
  demi::editor::EditorHudDocument document;
  std::string error;
  assert(document.open(path, error));
  const auto original = document.json();
  const auto defaults = document.prefabArguments("alpha.dot", error);
  assert(defaults && defaults->at("title") == "Shared");
  assert(document.json() == original);
  assert(document.prefabOrigin("alpha.dot.title")->localNodeId == "title");
  assert(document.prefabOrigin("alpha.dot.nested.caption")->localNodeId ==
         "nested.caption");
  assert(!document.prefabOrigin("alpha.dot.unrelated"));
  assert(document.setNodeField("alpha.dot", "size", Json::array({320, 180}),
                               error));
  assert(document.authoredNode("alpha.dot")
             ->at("overrides")
             .at("$root")
             .at("size") == Json::array({320, 180}));
  assert(document.setNodeField("alpha.dot.title", "text", "Local", error));
  assert(document.effectiveNode("alpha.dot.title")->at("text") == "Local");
  assert(document.effectiveNode("beta.title")->at("text") == "Shared");
  assert(document.setNodeField("alpha.dot.nested.caption", "text",
                               "Nested override", error));
  assert(document.authoredNode("alpha.dot")
             ->at("overrides")
             .at("nested.caption")
             .at("text") == "Nested override");
  assert(document.setNodeField("alpha.dot.title", "dock", "right", error));
  const auto *effective = document.effectiveNode("alpha.dot.title");
  assert(effective->at("dock") == "right" &&
         !effective->contains("anchor_min"));
  const auto title =
      std::ranges::find(document.preview().nodes, "alpha.dot.title",
                        &demi::runtime::ui::UiNode::id);
  assert(title != document.preview().nodes.end() && title->resolved.x == 200);
  assert(document.setNodeAnchors("alpha.dot.title", {0, 0}, {0, 0}, error));
  assert(!document.effectiveNode("alpha.dot.title")->contains("dock"));
  const auto beforeInvalid = document.json();
  assert(!document.setNodeField("alpha.dot.title", "id", "renamed", error));
  assert(document.json() == beforeInvalid);
  assert(!document.setNodeField("alpha.dot.missing", "text", "No", error));
  assert(document.json() == beforeInvalid);
  assert(document.resetNodeOverride("alpha.dot.title", "text", error));
  assert(document.effectiveNode("alpha.dot.title")->at("text") == "Shared");
  assert(document.undo(error));
  assert(document.json() == beforeInvalid);
  assert(document.redo(error));
  assert(document.resetNodeOverride("alpha.dot.title", {}, error));
  assert(!document.nodeOverrides("alpha.dot.title"));
  assert(document.setNodeField("alpha.dot", "arguments",
                               Json{{"title", "Argument"}}, error));
  assert(document.effectiveNode("alpha.dot.title")->at("text") == "Argument");
  assert(document.effectiveNode("beta.title")->at("text") == "Shared");
  assert(document.save(error));
  std::ifstream sourceFile(root / "ui/card.ui.prefab.json");
  const std::string unchanged{std::istreambuf_iterator<char>(sourceFile), {}};
  assert(unchanged == source);
  demi::editor::EditorHudDocument reopened;
  assert(reopened.open(path, error));
  assert(reopened.json() == document.json());
  assert(reopened.effectiveNode("alpha.dot.nested.caption")->at("text") ==
         "Nested override");
  assert(
      reopened.setNodeField("alpha.dot", "arguments", Json::object(), error));
  assert(reopened.effectiveNode("alpha.dot.title")->at("text") == "Shared");
}

void verifyTabAuthoring(const std::filesystem::path &root) {
  using Json = nlohmann::json;
  std::filesystem::create_directories(root / "ui");
  const auto write = [](const auto &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
  };
  write(root / "demi.project.json", R"({"format_version":1})");
  const std::string prefab =
      R"({"format_version":1,"id":"ui-prefab://tabbed","root":{
    "id":"root","type":"container","size":[400,300],
    "action_effects":{"show_a":{"show":["page_a"],"hide":["page_b"]},"show_b":{"show":["page_b"],"hide":["page_a"]}},
    "children":[{"id":"tab_a","type":"button","action":"show_a","size":[100,40]},
      {"id":"tab_b","type":"button","action":"show_b","size":[100,40]},
      {"id":"page_a","type":"container","size":[400,200],"children":[{"id":"label","type":"label","text":"First"}]},
      {"id":"page_b","type":"container","visible":false,"size":[400,200],"children":[{"id":"second_label","type":"label","text":"Second"}]}]}})";
  write(root / "ui/tabbed.ui.prefab.json", prefab);
  write(root / "ui/child.ui.prefab.json",
        R"({"format_version":1,"id":"ui-prefab://child",
    "parameters":{"caption":{"type":"string","default":"Child"}},"root":{"id":"child","type":"container","children":[
      {"id":"caption","type":"label","text":"${caption}"}]}})");
  const auto path = root / "tabs.hud.json";
  write(
      path,
      R"({"format_version":1,"children":[{"id":"tabs","prefab":"ui-prefab://tabbed"},{"id":"other","prefab":"ui-prefab://tabbed"}]})");
  demi::editor::EditorHudDocument document;
  std::string error;
  assert(document.open(path, error));
  const auto original = document.json();
  const auto visible = [&](const std::string &id) {
    const auto found = std::ranges::find(document.preview().nodes, id,
                                         &demi::runtime::ui::UiNode::id);
    return found != document.preview().nodes.end() && found->visible;
  };
  assert(!visible("tabs.page_b") && visible("tabs.page_a"));
  assert(document.previewNodeAction("tabs.tab_b"));
  assert(visible("tabs.page_b") && !visible("tabs.page_a") &&
         visible("other.page_a"));
  assert(document.json() == original && !document.canUndo() &&
         !document.isDirty());
  const auto effects =
      document.authoringProperties("tabs")->at("action_effects");
  assert(effects.contains("tabs.show_b") && !effects.contains("show_b"));
  assert(document.setNodeField("tabs", "action_effects", effects, error));
  assert(document.previewNodeAction("tabs.tab_a") && visible("tabs.page_a"));
  assert(document.previewNodeAction("tabs.tab_b") && visible("tabs.page_b"));
  assert(document.resetNodeOverride("tabs", "action_effects", error));
  std::string added;
  assert(document.createNode("label", "tabs.page_b", added, error,
                             demi::runtime::Vec2{20, 30}));
  assert(added == "label" && visible("tabs.page_b"));
  assert(document.authoredNode(added) && !document.prefabOrigin(added));
  assert(
      document.setNodeField(added, "text", "Local second-page content", error));
  assert(document.authoredNode(added)->at("text") ==
         "Local second-page content");
  assert(document.authoredNode("tabs")
             ->at("overrides")
             .at("page_b")
             .at("children")
             .size() == 1);
  assert(!document.authoredNode("tabs")
              ->at("overrides")
              .at("page_b")
              .contains("visible"));
  assert(document.deleteNode("tabs.second_label", error));
  assert(document.authoredNode("tabs")
             ->at("overrides")
             .at("second_label")
             .is_null());
  assert(!document.effectiveNode("tabs.second_label"));
  assert(document.undo(error));
  assert(document.effectiveNode("tabs.second_label"));
  assert(document.redo(error));
  assert(document.resetPrefabTarget("tabs", "second_label", error));
  std::string nested;
  assert(document.createPrefabInstance("ui-prefab://child", "tabs.page_b",
                                       nested, error));
  assert(document.authoredNode(nested)->at("prefab") == "ui-prefab://child");
  assert(document.setNodeField(nested, "arguments",
                               Json{{"caption", "Local argument"}}, error));
  assert(document.effectiveNode(nested + ".caption")->at("text") ==
         "Local argument");
  assert(document.setNodeField(nested + ".caption", "text", "Nested override",
                               error));
  assert(
      document.authoredNode(nested)->at("overrides").at("caption").at("text") ==
      "Nested override");
  std::string localContainer;
  assert(document.createNode("container", "ui_root", localContainer, error));
  assert(document.reparentNode("tabs.label", localContainer, error));
  assert(document.deleteNode(localContainer, error));
  assert(!document.effectiveNode(localContainer) &&
         !document.effectiveNode("tabs.label"));
  assert(document.undo(error));
  assert(document.effectiveNode("tabs.label")->at("parent") == localContainer);
  assert(document.undo(error));
  assert(document.undo(error));
  // Cross-instance moves keep local IDs and inherited links; unpack bakes only
  // ownership.
  assert(document.reparentNode(added, "other.page_b", error));
  assert(document.effectiveNode(added)->at("parent") == "other.page_b");
  assert(document.authoredNode(added)->at("text") ==
         "Local second-page content");
  assert(document.undo(error));
  assert(document.reparentNode(nested, "other.page_b", error));
  assert(document.effectiveNode(nested + ".caption")->at("text") ==
         "Nested override");
  assert(document.undo(error));
  assert(document.reparentNode("tabs.label", "other.page_b", error));
  assert(document.effectiveNode("tabs.label")->at("parent") == "other.page_b");
  assert(!document.reparentNode("other.page_b", "tabs.label", error));
  const auto linked = document.json();
  assert(document.unpackPrefab("tabs", error));
  assert(document.authoredNode("tabs.label")->at("parent") == "other.page_b");
  assert(document.authoredNode(nested + ".caption")->at("text") ==
         "Nested override");
  assert(document.authoredNode("tabs.page_b")->at("visible") == false);
  assert(document.previewNodeAction("tabs.tab_a"));
  assert(visible("tabs.page_a") && !visible("tabs.page_b"));
  assert(document.undo(error) && document.json() == linked);
  assert(document.undo(error));
  assert(document.previewNodeAction("tabs.tab_b"));
  const auto populated = document.json();
  assert(document.resetNodeOverride("tabs.page_b", "children", error));
  assert(!document.effectiveNode(added) && !document.effectiveNode(nested));
  assert(document.json() == original);
  assert(document.undo(error) && document.json() == populated);
  assert(document.save(error));
  assert(read(root / "ui/tabbed.ui.prefab.json") == prefab);
  demi::editor::EditorHudDocument reopened;
  assert(reopened.open(path, error));
  assert(!reopened.hasPreviewState() && !reopened.isDirty());
  assert(reopened.previewNodeAction("tabs.tab_b"));
  assert(reopened.effectiveNode(added)->at("text") ==
         "Local second-page content");
  assert(document.deleteNode(nested, error));
  assert(!document.effectiveNode(nested));
  assert(document.deleteNode(added, error));
  assert(document.resetPreviewState(error));
  assert(document.json() == original);
  assert(visible("tabs.page_a") && !visible("tabs.page_b"));
}

void verifyVisibleDropTargets() {
  using namespace demi::runtime::ui;
  UiDocument document;
  UiNode root;
  root.id = "root";
  root.type = "container";
  root.resolved = {0, 0, 400, 300};
  UiNode active;
  active.id = "display";
  active.type = "container";
  active.parent = "root";
  active.resolved = {0, 40, 400, 200};
  UiNode hidden = active;
  hidden.id = "audio";
  hidden.visible = false;
  UiNode label;
  label.id = "hidden_label";
  label.type = "label";
  label.parent = "audio";
  label.text = "Hidden";
  label.resolved = {20, 50, 100, 30};
  document.nodes = {root, active, hidden, label};
  assert(demi::editor::pickEditorHudNode(document, {25, 55}) == nullptr);
  const auto *target =
      demi::editor::pickEditorHudDropParent(document, {25, 55});
  assert(target && target->id == "display");
  document.nodes[1].visible = false;
  document.nodes[2].visible = true;
  assert(demi::editor::pickEditorHudNode(document, {25, 55})->id ==
         "hidden_label");
  assert(demi::editor::pickEditorHudDropParent(document, {25, 55})->id ==
         "audio");
}

void verifyTextEditSessions(const std::filesystem::path &root) {
  const auto path = root / "text.hud.json";
  {
    std::ofstream output(path);
    output
        << R"({"format_version":1,"children":[{"id":"label","type":"label","text":"Label"}]})";
  }
  demi::editor::EditorHudDocument document;
  std::string error;
  assert(document.open(path, error));
  const auto original = document.json();
  assert(document.setNodeField("label", "text", "P", error, true));
  assert(document.setNodeField("label", "text", "Power: 12 kW", error, true));
  // Saving while the field is active includes the latest keystroke.
  assert(document.save(error));
  demi::editor::EditorHudDocument reopened;
  assert(reopened.open(path, error));
  assert(reopened.authoredNode("label")->at("text") == "Power: 12 kW");
  document.endContinuousEdit();
  document.endContinuousEdit(); // Enter followed by blur adds no history.
  assert(document.undo(error));
  assert(document.json() == original && !document.canUndo());
  assert(document.redo(error));
  assert(document.setNodeField("label", "text", "Power: 13 kW", error, true));
  document.endContinuousEdit();
  assert(document.undo(error));
  assert(document.authoredNode("label")->at("text") == "Power: 12 kW");
  // A cancelled session cannot absorb an unrelated command underneath it.
  assert(document.setNodeField("label", "visible", false, error));
  const auto beforeCancelled = document.json();
  assert(document.setNodeField("label", "text", "draft", error, true));
  assert(document.setNodeField("label", "text", "Power: 12 kW", error, true));
  assert(document.json() == beforeCancelled);
  assert(document.setNodeField("label", "text", "final", error, true));
  assert(document.undo(error));
  assert(document.json() == beforeCancelled);
  assert(document.undo(error));
  assert(!document.authoredNode("label")->contains("visible"));
}

void verifyDockPresets(const std::filesystem::path &root) {
  using Json = nlohmann::json;
  struct Expected {
    const char *dock;
    float x, y, width, height;
  };
  const Expected cases[]{
      {"fill", 4, 6, 948, 524},     {"top", 4, 6, 948, 130},
      {"bottom", 4, 400, 948, 130}, {"left", 4, 6, 250, 524},
      {"right", 702, 6, 250, 524},  {"center", 353, 203, 250, 130}};
  const auto path = root / "dock.hud.json";
  for (const char *offset : {"position", "at"}) {
    for (const auto &expected : cases) {
      Json source =
          Json::parse(R"({"format_version":1,"canvas_size":[960,540],"root":{
        "id":"root","type":"container","dock":"fill","children":[{
          "id":"panel","type":"panel","anchor_min":[0.2,0.1],"anchor_max":[0.2,0.1],
          "size":[240,120],"min_size":[250,130],"margin":[4,6,8,10],"pad":5,
          "background_color":"#224466FF"}]}})");
      source["root"]["children"][0][offset] = {217.9, 79.8};
      {
        std::ofstream out(path);
        out << source.dump(2);
      }
      demi::editor::EditorHudDocument document;
      std::string error;
      assert(document.open(path, error));
      // Choosing a preset clears manual placement; the runtime owns alignment.
      assert(document.setNodeField("panel", "dock", expected.dock, error));
      const auto &bounds = document.preview().nodes[1].resolved;
      assert(bounds.x == expected.x && bounds.y == expected.y &&
             bounds.width == expected.width &&
             bounds.height == expected.height);
      const auto *node = document.authoredNode("panel");
      assert(!node->contains("position"));
      assert(!node->contains("at") && !node->contains("anchor_min") &&
             !node->contains("anchor_max"));
      for (const char *field :
           {"size", "min_size", "margin", "pad", "background_color"})
        assert(node->at(field) == source["root"]["children"][0][field]);
      const auto aligned = document.json();
      assert(document.setNodeField("panel", "dock", expected.dock, error));
      assert(document.json() == aligned);
      assert(document.undo(error));
      assert(document.json() == source && !document.canUndo());
      assert(document.redo(error));
      assert(document.json() == aligned);
      assert(!document.setNodeField("panel", "dock", "diagonal", error));
      assert(document.json() == aligned);
      assert(document.save(error));
      demi::editor::EditorHudDocument reopened;
      assert(reopened.open(path, error));
      assert(reopened.json() == aligned);
      const auto dockBounds = reopened.preview().nodes[1].resolved;
      assert(reopened.setNodeField("panel", "dock", nullptr, error));
      const auto freeBounds = reopened.preview().nodes[1].resolved;
      assert(dockBounds.x == freeBounds.x && dockBounds.y == freeBounds.y &&
             dockBounds.width == freeBounds.width &&
             dockBounds.height == freeBounds.height);
    }
  }
  // Reapplying an already-authored Top preset must remove its old drag offset.
  {
    std::ofstream out(path);
    out << R"({"format_version":1,"canvas_size":[960,540],
    "children":[{"id":"panel","type":"panel","dock":"top","position":[217.9,79.8],"size":[240,120]}]})";
  }
  demi::editor::EditorHudDocument implicit;
  std::string error;
  assert(implicit.open(path, error));
  assert(implicit.preview().nodes[1].resolved.x == 217.9F);
  assert(implicit.setNodeField("panel", "dock", "top", error));
  assert(implicit.preview().nodes[1].resolved.x == 0 &&
         implicit.preview().nodes[1].resolved.y == 0);
  assert(implicit.preview().nodes[1].resolved.width == 960);
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
  nlohmann::json arguments = prefab->at("arguments");
  assert(document.setNodeField(prefabId, "visible", false, error));
  assert(document.authoredNode(prefabId)
             ->at("overrides")
             .at("$root")
             .at("visible") == false);
  assert(document.undo(error));
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

  assert(!hierarchy.duplicateNode("root", duplicateId, error));
  assert(!hierarchy.duplicateNode("card.title", duplicateId, error));
  assert(hierarchy.json() == hierarchyBeforeFailures);

  // Moving local and inherited nodes preserves identity and the shared source.
  assert(hierarchy.reparentNode("group", "card", error));
  assert(hierarchy.effectiveNode("group")->at("parent") == "card");
  assert(hierarchy.authoredNode("group")->at("children")[0].at("id") ==
         "child");
  assert(hierarchy.undo(error));
  assert(hierarchy.json() == hierarchyBeforeFailures);
  assert(hierarchy.reparentNode("card.title", "destination", error));
  assert(hierarchy.nodeOverrides("card.title")->at("parent") == "destination");
  assert(hierarchy.effectiveNode("card.title")->at("parent") == "destination");
  assert(hierarchy.setNodeField("card.title", "text", "Moved title", error));
  const auto beforeUnpack = hierarchy.json();
  const auto sourceBeforeUnpack = read(root / "ui/card.ui.prefab.json");
  assert(hierarchy.unpackPrefab("card.title", error));
  assert(hierarchy.authoredNode("card") &&
         !hierarchy.authoredNode("card")->contains("prefab"));
  assert(hierarchy.authoredNode("card.title")->at("text") == "Moved title");
  assert(hierarchy.effectiveNode("card.title")->at("parent") == "destination");
  assert(!hierarchy.prefabOrigin("card.title"));
  assert(hierarchy.save(error));
  demi::editor::EditorHudDocument unpacked;
  assert(unpacked.open(hierarchyPath, error));
  assert(unpacked.effectiveNode("card.title")->at("parent") == "destination");
  assert(hierarchy.undo(error));
  assert(hierarchy.json() == beforeUnpack);
  assert(hierarchy.redo(error));
  assert(read(root / "ui/card.ui.prefab.json") == sourceBeforeUnpack);

  assert(!document.setCanvasSize({0.0F, 360.0F}, error));
  demi::editor::EditorHudDocument uiPrefab;
  assert(uiPrefab.open(root / "ui/card.ui.prefab.json", error));
  assert(!uiPrefab.setCanvasSize({640.0F, 360.0F}, error));
  verifyVisibleDropTargets();
  verifyTextEditSessions(root);
  verifyPrefabInstanceOverrides(root / "overrides");
  verifyTabAuthoring(root / "tabs");
  verifyDockPresets(root);
  fs::remove_all(root, ignored);
}
