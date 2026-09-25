#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/PackageContent.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/packages/PackageLock.h"
#include "demi/runtime/platform/ProjectFileWatcher.h"
#include "demi/schema/Validation.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

namespace {
using Json = nlohmann::json;
void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  assert(output.good());
}
std::size_t count(const demi::Diagnostics &diagnostics,
                  const std::string &code) {
  return std::ranges::count(diagnostics, code, &demi::Diagnostic::code);
}
} // namespace

int main() {
  namespace fs = std::filesystem;
  using namespace demi;
  const auto root =
      fs::temp_directory_path() /
      ("demi-discovery-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(fs::create_directory(root));
  Json project = {{"format_version", 1},
                  {"name", "Discovery"},
                  {"main_scene", "scene://test/main"},
                  {"scenes", Json::array({{{"id", "scene://test/main"}}})}};
  write(root / "demi.project.json", project.dump());
  write(root / "scenes/main.scene.json",
        R"({"format_version":1,"id":"scene://test/main","entities":[]})");
  write(root / "scenes/level.v2/extra.scene.json",
        R"({"format_version":1,"id":"scene://test/extra","entities":[]})");
  write(root / "assets/data/values.json", R"({"format_version":1,"value":2})");
  assert(!hasErrors(
      assets::importAsset({.projectDirectory = root,
                           .source = root / "assets/data/values.json",
                           .id = "asset://test/data",
                           .type = "DataAsset"})
          .diagnostics));
  const auto baseline = collectKnownSourceFiles(root);
  write(root / "assets/generated/atlas-data.json", R"({"format_version":1})");
  assert(!hasErrors(
      assets::importAsset({.projectDirectory = root,
                           .source = root / "assets/generated/atlas-data.json",
                            .id = "asset://generated/atlas-data",
                           .type = "DataAsset"})
          .diagnostics));
  for (const auto &directory :
       {".kilo/worktrees/other", ".vscode/copy", ".tool-state", "build/old",
        "generated/cooked", "node_modules/module", "__pycache__",
        "assets/.kilo/worktrees/other"}) {
    write(root / directory / "demi.project.json", "{invalid");
    write(root / directory / "scenes/foreign.scene.json", "{invalid");
    write(root / directory / "foreign.asset.json", "{invalid");
  }
  fs::create_directory_symlink(root / ".kilo/worktrees/other",
                               root / "linked-tool-tree");
  assert(collectKnownSourceFiles(root) == baseline);
  const auto registry = loadAuthoredAssetRegistry(root);
  assert(registry.assets.size() == 2 && registry.diagnostics.empty());
  assert(!hasErrors(validateProjectPath(root).diagnostics));
  assert(hasErrors(
      validatePath(root / ".kilo/worktrees/other/scenes/foreign.scene.json")
          .diagnostics));

  editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root, error));
  assert(!hasErrors(workspace.diagnostics()));
  assert(!workspace.sourceDirectories().contains(".kilo"));
  for (const auto &source : workspace.sources())
    assert(source.string().find(".kilo") == std::string::npos);
  assert(!workspace.createFolder({}, ".hidden-tool", error));
  runtime::platform::ProjectFileWatcher watcher;
  watcher.reset(root);
  write(root / ".kilo/worktrees/other/scripts/ignored.lua", "not valid lua!");
  assert(watcher.poll().empty());
  write(root / "scripts/game.lua", "return {}\n");
  assert(watcher.poll().changed.size() == 1);
  const auto cooked = root / "build/cooked";
  assert(
      !hasErrors(assets::cookProject({.projectFile = root / "demi.project.json",
                                      .outputDirectory = cooked,
                                      .platform = "linux"})));
  assert(!fs::exists(cooked / ".kilo") && !fs::exists(cooked / "node_modules"));
  assert(fs::is_regular_file(cooked / "scripts/game.lua"));

  // Excluding .demi from discovery must not bypass declared dependencies.
  packages::PackageManifest manifest{
      .name = "demi.test",
      .version = *packages::SemanticVersion::parse("1.0.0"),
      .engineVersion = *packages::VersionConstraint::parse("^0.1.0"),
      .publicModules = {"demi.test"},
      .files = {"scripts/demi/test.lua", "groups/test.asset-group.json"}};
  packages::PackageRelease release{.manifest = manifest,
                                   .archiveHash =
                                       "sha256:" + std::string(64, '0'),
                                   .archiveUri = "file:///unused.demipkg"};
  write(root / "demi.packages.lock.json",
        packages::packageLockJson({{"demi.test", release}},
                                  "file:///unused-registry")
            .dump());
  project["packages"] = {{"demi.test", "^1.0.0"}};
  write(root / "demi.project.json", project.dump());
  const auto missing = validateProjectPath(root).diagnostics;
  assert(count(missing, "PACKAGE_MANIFEST_READ_FAILED") == 1);
  assert(count(missing, "PACKAGE_CONTENT_LOCK_MISMATCH") == 0);
  const auto package = root / ".demi/packages/demi.test";
  write(
      package / "groups/test.asset-group.json",
      R"({"format_version":1,"id":"asset-group://test/package","roots":["asset://test/data"]})");
  auto changed = manifest;
  changed.version = *packages::SemanticVersion::parse("1.0.1");
  write(package / "demi.package.json",
        packages::packageManifestJson(changed).dump());
  assert(count(validateProjectPath(root).diagnostics,
               "PACKAGE_CONTENT_LOCK_MISMATCH") == 1);
  write(package / "demi.package.json",
        packages::packageManifestJson(manifest).dump());
  assert(count(validateProjectPath(root).diagnostics,
               "PACKAGE_CONTENT_FILE_MISSING") == 1);
  write(package / "scripts/demi/test.lua", "return {}\n");
  const auto content = assets::loadLockedPackageContent(root, "");
  assert(!hasErrors(content.diagnostics) && content.files.size() == 2);
  project["assets"] = Json::array({"asset-group://test/package"});
  write(root / "demi.project.json", project.dump());
  const auto sources = collectProjectSourceFiles(loadAssetRegistry(root));
  assert(std::ranges::find(sources, package / "groups/test.asset-group.json") !=
         sources.end());
  assert(!hasErrors(validateProjectPath(root).diagnostics));
  write(package / "groups/test.asset-group.json", "{invalid");
  assert(hasErrors(validateProjectPath(root).diagnostics));

  Diagnostic original{.severity = Severity::Error,
                      .code = "TEST",
                      .message = "same error",
                      .path = "scene.json",
                      .line = 1};
  auto elsewhere = original;
  elsewhere.line = 2;
  Diagnostics duplicates{original, original, elsewhere};
  deduplicateDiagnostics(duplicates);
  assert(duplicates.size() == 2 && duplicates[0].line == 1 &&
         duplicates[1].line == 2);
  fs::remove_all(root);
}
