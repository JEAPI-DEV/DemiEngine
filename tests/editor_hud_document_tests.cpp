#include "editor/EditorHudCanvas.h"
#include "editor/EditorHudDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>

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
  assert(document.deleteNode("button", error));
  assert(document.preview().nodes.size() == 1);
  assert(!document.deleteNode("root", error));
  assert(document.undo(error));
  assert(document.preview().nodes.size() == 2);
  assert(document.save(error));
  fs::remove_all(root, ignored);
}
