#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace demi::runtime;
namespace {
void writeScript(const std::filesystem::path &path, int version) {
  std::ofstream output(path);
  output << "local Transform = require('demi.transform2d')\n"
            "local Script = {}\n"
            "function Script:on_start()\n"
            "  Transform.set_position(self.entity_id, "
         << version
         << ", 0)\n"
            "end\n"
            "function Script:on_update(dt)\n"
            "  local x,y = Transform.get_position(self.entity_id)\n"
            "  Transform.set_position(self.entity_id, x, y + 1)\n"
            "end\n"
            "return Script\n";
  assert(output.good());
}

void checkReloads(const std::filesystem::path &root, bool external) {
  std::filesystem::create_directories(root / "scripts");
  const auto script = root / "scripts/actor.lua";
  writeScript(script, 1);
  World world;
  world.id = "scene://reload/main";
  for (int index = 0; index < 16; ++index) {
    Entity entity;
    entity.id = "actor_" + std::to_string(index);
    entity.setComponent(Transform2DComponent{});
    entity.setComponent(
        LuaScriptComponent{.module = "script://scripts/actor.lua"});
    world.entities.push_back(std::move(entity));
  }
  InputState input;
  ProjectData project;
  project.name = "Script reload tests";
  project.projectDirectory = root;
  LuaScriptHost host;
  std::string error;
  assert(host.initialize(world, input, nullptr, error));
  host.setHotReloadEnabled(true, external);
  assert(host.loadWorldScripts(project, world, error));
  host.start();
  const auto allAt = [&](float x) {
    return std::ranges::all_of(world.entities, [&](const Entity &entity) {
      return entity.component<Transform2DComponent>()->position.x == x;
    });
  };
  assert(allAt(1));
  for (int frame = 0; frame < 50; ++frame) {
    host.update(0.016F);
  }
  assert(world.entities.front().component<Transform2DComponent>()->position.y ==
         50);

  const auto previousTime = std::filesystem::last_write_time(script);
  writeScript(script, 2);
  if (external) {
    // An explicit change notification is authoritative even if mtime
    // granularity hid the edit. Deduplicate one source, but reload every live
    // instance.
    std::filesystem::last_write_time(script, previousTime);
    host.update(0.016F);
    assert(allAt(1));
    const std::array paths{script,
                           script.parent_path() / "../scripts/actor.lua"};
    host.notifyScriptChanges(paths);
  }
  const auto awaitVersion = [&](float version) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
      host.update(0.016F);
      if (allAt(version)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
  };
  assert(awaitVersion(2));
  host.update(0.016F);
  assert(world.entities.front().component<Transform2DComponent>()->position.y ==
         2);

  {
    std::ofstream output(script);
    output << "this is not valid Lua !!!\n";
  }
  const std::array changed{script};
  if (external) {
    host.notifyScriptChanges(changed);
  }
  host.update(0.016F);
  assert(allAt(2));
  assert(world.entities.front().component<Transform2DComponent>()->position.y ==
         3);

  std::filesystem::remove(script);
  if (external) {
    host.notifyScriptChanges(changed);
  }
  host.update(0.016F);
  assert(allAt(2));
  writeScript(script, 3);
  if (external) {
    host.notifyScriptChanges(changed);
  }
  assert(awaitVersion(3));

  host.setHotReloadEnabled(false);
  writeScript(script, 4);
  host.notifyScriptChanges(changed);
  host.update(0.016F);
  assert(allAt(3));
  host.destroy();
}
} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "demi-script-reload-tests";
  std::filesystem::remove_all(root);
  checkReloads(root / "external", true);
  checkReloads(root / "standalone", false);
  std::filesystem::remove_all(root);
  std::cout << "Script reload notifications, native watching and last-good "
               "tables passed\n";
}
