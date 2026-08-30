#include "editor/EditorAssetGroupDocument.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_asset_group_document_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  const fs::path path = root / "startup.asset-group.json";
  std::ofstream(path)
      << R"({"format_version":1,"id":"asset-group://startup","roots":["asset://ui/logo"],"budget":{"resident_mb":32,"decoded_mb":8,"upload_ms_per_frame":2}})";

  demi::editor::EditorAssetGroupDocument document;
  std::string error;
  assert(document.open(path, error));
  assert(document.setRoots({"scene://main", "asset://ui/logo"}, error));
  assert(document.isDirty());
  assert(document.undo(error));
  assert(document.roots() == std::vector<std::string>{"asset://ui/logo"});
  assert(document.redo(error));
  assert(document.roots().size() == 2);
  assert(!document.setRoots({}, error));
  assert(document.save(error));
  assert(!document.isDirty());

  fs::remove_all(root, ignored);
}
