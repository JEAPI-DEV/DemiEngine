#include "editor/EditorProjectDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    "demi_editor_project_document_tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  write(
      root / "demi.project.json",
      R"({"format_version":1,"name":"Project","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}],"assets":[]})");

  demi::editor::EditorProjectDocument document;
  std::string error;
  assert(document.open(root / "demi.project.json", error));
  assert(document.setPreloadedAssets(
      {"asset://ui/logo", "asset-group://chapter"}, error));
  assert(document.isDirty());
  assert(document.undo(error));
  assert(document.preloadedAssets().empty());
  assert(document.redo(error));
  assert(document.preloadedAssets().size() == 2);
  assert(!document.setPreloadedAssets({"asset://ui/logo", "asset://ui/logo"},
                                      error));
  assert(document.addScene("scene://menu", "scenes/menu.scene.json", error));
  assert(document.scenes().size() == 2);
  assert(!document.removeScene("scene://main", error));
  assert(document.removeScene("scene://menu", error));
  assert(document.save(error));
  assert(!document.isDirty());

  std::filesystem::remove_all(root, ignored);
}
