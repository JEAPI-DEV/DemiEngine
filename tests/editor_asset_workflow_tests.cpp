#include "editor/EditorWorkspace.h"

#include "demi/schema/Validation.h"

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
