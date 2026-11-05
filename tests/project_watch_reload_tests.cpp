#include "demi/runtime/app/ReloadCoordinator.h"
#include "demi/runtime/platform/ProjectFileWatcher.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

void write(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
  assert(output.good());
}

bool hasCode(const demi::Diagnostics &diagnostics, const std::string &code) {
  for (const auto &diagnostic : diagnostics)
    if (diagnostic.code == code)
      return true;
  return false;
}

void targetedChangesInLargeTree() {
  namespace fs = std::filesystem;
  using demi::runtime::platform::ProjectFileWatcher;
  const auto root = fs::temp_directory_path() / "demi_watch_targeted_tests";
  const auto outside =
      fs::temp_directory_path() / "demi_watch_targeted_outside";
  std::error_code error;
  fs::remove_all(root, error);
  fs::remove_all(outside, error);
  for (int directory = 0; directory < 32; ++directory) {
    for (int file = 0; file < 64; ++file) {
      write(root / std::to_string(directory) / (std::to_string(file) + ".lua"),
            "return 1");
    }
  }
  ProjectFileWatcher watcher;
  watcher.reset(root);
  assert(watcher.poll().empty());
  const auto first = root / "10/17.lua";
  const auto second = root / "20/23.lua";
  write(first, "return 22");
  auto changes = watcher.poll();
  assert(changes.generation == 1);
  assert(changes.changed == std::vector<fs::path>{first});
  assert(changes.removed.empty());
  write(second, "return 333");
  write(first, "return 4444");
  changes = watcher.poll();
  assert(changes.generation == 2);
  assert((changes.changed == std::vector<fs::path>{first, second}));
  assert(changes.removed.empty());
  fs::remove(first);
  changes = watcher.poll();
  assert(changes.generation == 3 && changes.changed.empty());
  assert(changes.removed == std::vector<fs::path>{first});
  const auto added = root / "10/added.lua";
  write(added, "return 5");
  changes = watcher.poll();
  assert(changes.generation == 4);
  assert(changes.changed == std::vector<fs::path>{added});

  write(root / "generated/ignored.lua", "generated");
  write(root / "saves/ignored.lua", "save");
  write(outside / "external.lua", "return 6");
  fs::create_directory_symlink(outside, root / "linked-directory");
  assert(watcher.poll().empty());
  write(outside / "external.lua", "return 77");
  assert(watcher.poll().empty());
  // Existing discovery admits file symlinks but never descends directory ones.
  fs::create_symlink(outside / "external.lua", root / "alias.lua");
  changes = watcher.poll();
  assert(changes.changed == std::vector<fs::path>{root / "alias.lua"});
  fs::remove(root / "alias.lua");
  changes = watcher.poll();
  assert(changes.removed == std::vector<fs::path>{root / "alias.lua"});
  fs::remove_all(root, error);
  fs::remove_all(outside, error);
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  using demi::runtime::ReloadCoordinator;
  using demi::runtime::platform::ProjectFileWatcher;

  targetedChangesInLargeTree();

  const fs::path root = fs::temp_directory_path() / "demi_watch_reload_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  write(root / "demi.project.json", R"({
    "format_version":1,"name":"Watch Test",
    "main_scene":"scene://watch/main",
    "scenes":[{"id":"scene://watch/main","path":"scenes/main.scene.json"}],
    "scripting":{"language":"lua54","modules":[]}
  })");
  write(root / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://watch/main","name":"Main",
    "entities":[{"id":"script","components":{"LuaScript":{"module":"script://scripts/main.lua"}}}]
  })");
  write(root / "scripts/main.lua", "local M = {}\nreturn M\n");

  ProjectFileWatcher watcher;
  watcher.reset(root);
  assert(watcher.poll().empty());

  write(root / "scripts/main.lua",
        "local M = {}\nfunction M:on_start() end\nreturn M\n");
  const auto luaBatch = watcher.poll();
  assert(luaBatch.generation == 1);
  assert(luaBatch.changed.size() == 1);
  assert(watcher.poll().empty());

  int sceneReloads = 0;
  int sceneCancellations = 0;
  int assetReloads = 0;
  std::vector<fs::path> scriptChanges;
  ReloadCoordinator coordinator(
      root / "demi.project.json",
      {.reloadScene =
           [&](std::string &) {
             ++sceneReloads;
             return true;
           },
       .cancelSceneReload = [&] { ++sceneCancellations; },
       .reloadAssets =
           [&](std::string &) {
             ++assetReloads;
             return true;
           },
       .reloadScripts = [&](std::span<const fs::path> paths) {
         scriptChanges.insert(scriptChanges.end(), paths.begin(), paths.end());
       }});
  auto result = coordinator.process(luaBatch);
  assert(result.applied);
  assert(result.luaChanged);
  assert(scriptChanges == luaBatch.changed);
  scriptChanges.clear();
  assert(sceneReloads == 0 && assetReloads == 0);
  assert(!coordinator.process(luaBatch).applied); // stale generation

  write(root / "scenes/main.scene.json", "{ invalid");
  const auto invalidBatch = watcher.poll();
  result = coordinator.process(invalidBatch);
  assert(!result.applied);
  assert(demi::hasErrors(result.diagnostics));
  assert(sceneReloads == 0);

  write(root / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://watch/main","name":"Reloaded",
    "entities":[{"id":"script","components":{"LuaScript":{"module":"script://scripts/main.lua"}}}]
  })");
  result = coordinator.process(watcher.poll());
  assert(result.applied);
  assert(sceneReloads == 1);

  write(root / "scripts/main.lua", "this is not valid lua !!!");
  result = coordinator.process(watcher.poll());
  assert(!result.applied);
  assert(demi::hasErrors(result.diagnostics));

  assert(scriptChanges.empty()); // Rejected source never reaches the host.
  write(root / "scripts/main.lua", "local M = {}\nreturn M\n");
  result = coordinator.process(watcher.poll());
  assert(result.applied);
  write(root / "assets/pixel.png", "png-v1");
  result = coordinator.process(watcher.poll());
  assert(result.applied);
  assert(assetReloads == 1);

  write(root / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://watch/main","name":"Batch",
    "entities":[{"id":"script","components":{"LuaScript":{"module":"script://scripts/main.lua"}}}]
  })");
  write(root / "assets/pixel.png", "png-v2");
  ReloadCoordinator failingCoordinator(
      root / "demi.project.json",
      {.reloadScene =
           [&](std::string &) {
             ++sceneReloads;
             return true;
           },
       .cancelSceneReload = [&] { ++sceneCancellations; },
       .reloadAssets =
           [&](std::string &error) {
             error = "injected upload failure";
             return false;
           }});
  result = failingCoordinator.process(watcher.poll());
  assert(!result.applied);
  assert(hasCode(result.diagnostics, "RELOAD_ASSETS_REJECTED"));
  assert(sceneCancellations == 1);

  fs::remove(root / "scenes/main.scene.json", ignored);
  result = coordinator.process(watcher.poll());
  assert(!result.applied);
  assert(hasCode(result.diagnostics, "RELOAD_PROJECT_PREPARE_FAILED") ||
         demi::hasErrors(result.diagnostics));
  assert(sceneReloads == 2);

  // Generated output must never feed the watcher back into itself.
  write(root / "generated/runtime-cook/linux/output.bin", "generated");
  assert(watcher.poll().empty());

  // Directory events must install watches for newly created descendants,
  // including atomic editor saves and renamed/deleted subtrees.
  const auto nested = root / "scripts/new/deep/action.lua";
  write(nested, "return 1");
  auto changes = watcher.poll();
  assert(std::ranges::find(changes.changed, nested) != changes.changed.end());
  write(nested, "return 22");
  changes = watcher.poll();
  assert(std::ranges::find(changes.changed, nested) != changes.changed.end());
  const auto temporary = nested.parent_path() / "replacement.tmp";
  write(temporary, "return 333");
  fs::rename(temporary, nested);
  changes = watcher.poll();
  assert(std::ranges::find(changes.changed, nested) != changes.changed.end());
  fs::rename(root / "scripts/new", root / "scripts/moved");
  const auto moved = root / "scripts/moved/deep/action.lua";
  changes = watcher.poll();
  assert(std::ranges::find(changes.changed, moved) != changes.changed.end());
  assert(std::ranges::find(changes.removed, nested) != changes.removed.end());
  write(moved, "return 4444");
  changes = watcher.poll();
  assert(std::ranges::find(changes.changed, moved) != changes.changed.end());
  fs::remove_all(root, ignored);
  assert(!watcher.poll().removed.empty());
  write(root / "scripts/recreated.lua", "return 5");
  changes = watcher.poll();
  assert(std::ranges::find(changes.changed, root / "scripts/recreated.lua") !=
         changes.changed.end());
  for (int frame = 0; frame < 100; ++frame) {
    assert(watcher.poll().empty());
  }
  watcher.reset(root);
  assert(watcher.poll().empty());
  write(root / "scripts/recreated.lua", "return 66");
  assert(watcher.poll().generation == 1);

  fs::remove_all(root, ignored);
}
