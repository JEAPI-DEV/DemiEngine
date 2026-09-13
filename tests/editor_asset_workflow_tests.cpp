#include "editor/EditorWorkspace.h"

#include "demi/schema/Validation.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
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
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_asset_workflow_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  write(
      root / "project/demi.project.json",
      R"({"format_version":1,"name":"Asset Editor","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}],"assets":[]})");
  write(root / "project/scenes/main.scene.json",
        R"({"format_version":1,"id":"scene://main","entities":[]})");
  write(root / "external/logo.png", "png-fixture");

  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root / "project", error));
  const auto sourceCount = workspace.sources().size();
  assert(workspace.createFolder({}, "assets", error));
  assert(workspace.createFolder("assets", "Props", error));
  assert(workspace.createFolder("assets/Props", "Empty folder", error));
  assert(workspace.sourceDirectories().contains("assets/Props/Empty folder"));
  assert(fs::is_empty(root / "project/assets/Props/Empty folder"));
  assert(workspace.sources().size() == sourceCount);
  assert(!workspace.hasUnsavedChanges());
  assert(!workspace.createFolder("assets", "Props", error));
  for (const std::string name :
       {"", ".", "..", "../escape", "nested/child", "back\\slash", "   ",
        ".git", ".demi", "build", "generated"})
    assert(!workspace.createFolder("assets", name, error));
  assert(!workspace.createFolder("..", "escape", error));
  assert(!workspace.createFolder(root / "external", "escape", error));
  assert(!workspace.createFolder("assets/missing", "child", error));
  assert(!workspace.createFolder("assets", std::string("bad\0name", 8), error));
  fs::create_directory_symlink(root / "external",
                               root / "project/assets/linked");
  assert(!workspace.createFolder("assets/linked", "escape", error));
  assert(!fs::exists(root / "external/escape"));
  write(root / "project/assets/file.txt", "fixture");
  assert(!workspace.createFolder("assets", "file.txt", error));
  assert(!workspace.createFolder("assets/file.txt", "child", error));
  fs::create_directories(root / "project/generated/hidden");
  assert(!workspace.createFolder("generated", "child", error));
  fs::create_directories(root / "project/external_empty/nested");
  workspace.refreshAssetMetadata();
  assert(workspace.sourceDirectories().contains("external_empty/nested"));
  assert(!workspace.sourceDirectories().contains("generated"));
  assert(!workspace.sourceDirectories().contains("assets/linked"));
  assert(workspace.createEntity(error));
  const auto dirtyScene = workspace.sceneDocument().json();
  assert(workspace.createFolder("assets", "While editing", error));
  assert(workspace.sceneDocument().json() == dirtyScene);
  assert(workspace.sceneDocument().isDirty());
  assert(workspace.undo(error));
  demi::editor::EditorWorkspace reopened;
  assert(reopened.open(root / "project", error));
  assert(reopened.sourceDirectories().contains("assets/Props/Empty folder"));
  assert(workspace.importAsset(
      {.source = root / "external/logo.png", .id = "asset://ui/logo"}, error));
  assert(workspace.assetIndex().assets().size() == 1);
  const auto manifest = workspace.assetIndex().assets().front().manifest;
  assert(manifest.id == "asset://ui/logo");
  assert(workspace.createAssetGroup("asset-group://startup",
                                    {"asset://ui/logo"}, error));
  assert(workspace.assetIndex().groups().size() == 1);
  assert(workspace.setPreloadedAssets({"asset-group://startup"}, error));
  assert(workspace.projectDocument().isDirty());
  assert(workspace.projectUndo(error));
  assert(workspace.projectDocument().preloadedAssets().empty());
  assert(workspace.projectRedo(error));
  assert(workspace.saveProject(error));
  assert(!workspace.projectDocument().isDirty());
  assert(!demi::hasErrors(demi::validatePath(root / "project").diagnostics));

  write(manifest.sourcePath, "updated-png-fixture");
  workspace.refreshAssetMetadata();
  assert(!workspace.assetIndex().assets().front().diagnostics.empty());
  assert(workspace.reimportAsset(manifest.manifestPath, error));
  assert(!demi::hasErrors(demi::validatePath(root / "project").diagnostics));

  fs::remove_all(root, ignored);
}
