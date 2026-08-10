#include "demi/runtime/app/ReloadCoordinator.h"
#include "demi/runtime/platform/ProjectFileWatcher.h"

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

} // namespace

int main() {
  namespace fs = std::filesystem;
  using demi::runtime::ReloadCoordinator;
  using demi::runtime::platform::ProjectFileWatcher;

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
           }});
  auto result = coordinator.process(luaBatch);
  assert(result.applied);
  assert(result.luaChanged);
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

  fs::remove_all(root, ignored);
}
