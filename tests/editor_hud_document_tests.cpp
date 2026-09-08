#include "editor/EditorHudCanvas.h"
#include "editor/EditorHudDocument.h"

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
  assert(document.deleteNode("button", error));
  assert(document.preview().nodes.size() == 1);
  assert(!document.deleteNode("root", error));
  assert(document.undo(error));
  assert(document.preview().nodes.size() == 2);
  assert(document.save(error));
  const std::string saved = read(path);
  assert(saved.find("{\"format_version\":1,") == 0);
  const nlohmann::json savedDocument = nlohmann::json::parse(saved);
  assert(savedDocument["root"]["children"][0]["position"] ==
         nlohmann::json({28.7, 56.0}));
  assert(savedDocument["root"]["children"][0]["anchor_min"] ==
         nlohmann::json({0.33, 0.67}));
  fs::remove_all(root, ignored);
}
