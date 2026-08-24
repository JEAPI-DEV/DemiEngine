#include "editor/EditorAssetDrop.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_asset_drop_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  const fs::path source = root / "Player Portrait.PNG";
  std::ofstream(source) << "fixture";
  std::string error;
  const auto suggestion = demi::editor::suggestAssetImport(source, error);
  assert(suggestion);
  assert(suggestion->source == fs::absolute(source).lexically_normal());
  assert(suggestion->assetId == "asset://player_portrait");
  assert(!demi::editor::suggestAssetImport(root, error));
  assert(error.find("regular files") != std::string::npos);
  fs::remove_all(root, ignored);
}
