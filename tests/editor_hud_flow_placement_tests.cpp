#include "editor/EditorHudDocument.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using demi::editor::EditorHudDocument;
using demi::runtime::Vec2;
void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
void write(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << value.dump(2);
  require(out.good(), "Could not write fixture");
}
const auto &node(const EditorHudDocument &document, const std::string &id) {
  const auto &nodes = document.preview().nodes;
  const auto found =
      std::ranges::find(nodes, id, &demi::runtime::ui::UiNode::id);
  require(found != nodes.end(), "Missing preview node: " + id);
  return *found;
}
Json fixture(std::string_view layout) {
  Json result =
      Json::parse(R"({"format_version":1,"canvas_size":[640,360],"root":{
    "id":"root","type":"container","dock":"fill","children":[{
      "id":"group","type":"panel","position":[40,30],"size":[400,200],
      "padding":[10,20,10,20],"gap":10,"columns":2,"children":[
        {"id":"a","type":"label","text":"A","size":[80,30]},
        {"id":"hidden","type":"label","visible":false,"size":[80,30]},
        {"id":"b","type":"label","text":"B","size":[80,30]},
        {"id":"c","type":"label","text":"C","size":[80,30]}]}]}})");
  result["root"]["children"][0]["stack"] = layout;
  return result;
}
void check(const fs::path &root) {
  write(root / "demi.project.json", {{"format_version", 1}});
  const Json widget =
      Json::parse(R"({"format_version":1,"id":"ui-prefab://widget","root":{
    "id":"widget","type":"panel","dock":"center","position":[90,80],"size":[60,20],"children":[
      {"id":"text","type":"label","text":"Inside","position":[4,3],"size":[50,15]}]}})");
  write(root / "ui/widget.ui.prefab.json", widget);
  const auto path = root / "main.hud.json";
  struct Case {
    const char *layout;
    Vec2 point;
    std::size_t index;
  };
  const Case cases[]{
      {"row", {10, 5}, 0},    {"row", {100, 5}, 2},   {"row", {1000, 5}, 4},
      {"column", {5, 0}, 0},  {"column", {5, 50}, 2}, {"column", {5, 1000}, 4},
      {"grid", {10, 5}, 0},   {"grid", {210, 5}, 2},  {"grid", {10, 45}, 3},
      {"grid", {300, 45}, 4}, {"grid", {0, 1000}, 4}};
  for (const auto &test : cases)
    for (bool prefab : {false, true}) {
      const auto before = fixture(test.layout);
      write(path, before);
      EditorHudDocument document;
      std::string id, error;
      require(document.open(path, error), error);
      require(
          prefab ? document.createPrefabInstance("ui-prefab://widget", "group",
                                                 id, error, test.point)
                 : document.createNode("label", "group", id, error, test.point),
          error);
      const auto &children = document.authoredNode("group")->at("children");
      require(children.at(test.index).at("id") == id,
              "Wrong flow insertion order");
      require(children.size() == 5, "Drop did not insert one authored item");
      const auto &placed = node(document, id);
      require(placed.layout.position.x == 0 && placed.layout.position.y == 0,
              "Flow drop retained a root position offset");
      require(
          placed.layout.anchorMin.x == 0 && placed.layout.anchorMax.x == 0 &&
              placed.layout.anchorMin.y == 0 && placed.layout.anchorMax.y == 0,
          "Prefab root anchors escaped the flow slot");
      if (test.index == 0)
        require(placed.resolved.x == 50 && placed.resolved.y == 50,
                "First flow item ignored parent padding or position");
      if (prefab) {
        require(placed.layout.size.x == 60 && placed.layout.size.y == 20,
                "Prefab dimensions changed");
        require(node(document, id + ".text").layout.position.x == 4,
                "Prefab child layout changed");
      } else
        require(!children.at(test.index).contains("position"),
                "Authored a redundant flow position");
      const auto after = document.json();
      require(document.undo(error), error);
      require(document.json() == before && !document.canUndo(),
              "Drop was not one Undo step");
      require(document.redo(error), error);
      require(document.json() == after, "Redo changed the flow drop");
      require(document.save(error), error);
      EditorHudDocument reopened;
      require(reopened.open(path, error), error);
      require(reopened.json() == after,
              "Flow placement did not survive save/reopen");
    }
  // Empty and menu-created flow controls use the same zero-offset rule.
  for (const char *direction : {"row", "column", "grid"}) {
    auto source = fixture(direction);
    source["root"]["children"][0]["children"] = Json::array();
    write(path, source);
    EditorHudDocument document;
    std::string id, error;
    require(document.open(path, error), error);
    require(document.createNode("button", "group", id, error), error);
    require(node(document, id).layout.position.x == 0 &&
                node(document, id).layout.position.y == 0,
            "Menu creation kept default offset");
    const auto before = document.json();
    require(
        !document.createNode("label", "group", id, error,
                             Vec2{std::numeric_limits<float>::quiet_NaN(), 0}),
        "Accepted invalid coordinates");
    require(document.json() == before, "Rejected placement changed the source");
  }
  auto free = fixture("row");
  free["root"]["children"][0].erase("stack");
  write(path, free);
  EditorHudDocument document;
  std::string id, error;
  require(document.open(path, error), error);
  require(document.createNode("label", "group", id, error, Vec2{17, 29}),
          error);
  require(document.authoredNode(id)->at("position") == Json({17, 29}),
          "Free placement changed");
  require(document.createPrefabInstance("ui-prefab://widget", "group", id,
                                        error, Vec2{33, 44}),
          error);
  require(document.authoredNode(id)->at("overrides") ==
              Json({{"position", {33, 44}}}),
          "Free prefab placement changed");
  auto implicit = fixture("row");
  implicit["children"] = implicit["root"]["children"];
  implicit.erase("root");
  write(path, implicit);
  EditorHudDocument implicitDocument;
  require(implicitDocument.open(path, error), error);
  require(implicitDocument.createNode("label", "group", id, error, Vec2{0, 0}),
          error);
  require(implicitDocument.authoredNode("group")->at("children")[0]["id"] == id,
          "Implicit-root flow placement failed");
}
} // namespace
int main() {
  auto pattern = (fs::temp_directory_path() / "demi-hud-flow-XXXXXX").string();
  std::vector<char> bytes(pattern.begin(), pattern.end());
  bytes.push_back('\0');
  const char *directory = mkdtemp(bytes.data());
  if (!directory)
    return 1;
  const fs::path root = directory;
  try {
    check(root);
    fs::remove_all(root);
    std::cout << "HUD flow placement checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    fs::remove_all(root);
    return 1;
  }
}
