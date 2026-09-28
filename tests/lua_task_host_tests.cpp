#include "demi/runtime/scripting/LuaScriptHost.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void execute(demi::runtime::LuaScriptHost &host, const char *script) {
  const auto result = host.executeConsole(script);
  if (!result.succeeded) {
    std::cerr << result.error << '\n';
    std::abort();
  }
}
} // namespace

int main() {
  using namespace demi::runtime;
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-task-host-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(std::filesystem::create_directory(root));
  std::filesystem::create_directory(root / "scripts");
  {
    std::ofstream script(root / "scripts/main.lua");
    script << R"lua(
local Task = require('demi.task')
local Game = {}
function Game:on_start()
  urgent_calls = 0
  update_calls = 0
  busy_task = Task.fork(function()
    local Task = require('demi.task')
    while not Task.cancelled() do
      local sum = 0
      for i = 1, 10000 do sum = sum + i end
    end
  end)
end
-- @HandleAction("urgent")
function Game:on_urgent(event)
  urgent_calls = urgent_calls + 1
end
function Game:on_update(dt)
  update_calls = update_calls + 1
end
return Game
)lua";
  }
  {
    World world;
    world.hudCanvasSize = {100, 100};
    world.ui.canvasSize = {100, 100};
    ui::UiNode button;
    button.id = "urgent_button";
    button.type = "button";
    button.action = "urgent";
    button.layout.size = {100, 100};
    button.resolved = {0, 0, 100, 100};
    button.focusable = true;
    world.ui.nodes.push_back(button);
    InputState input;
    input.mousePosition = {50, 50};
    ProjectData project;
    project.projectDirectory = root;
    project.scriptEntry = "script://scripts/main.lua";
    LuaScriptHost host;
    std::string error;
    assert(host.initialize(world, input, nullptr, error));
    assert(host.loadWorldScripts(project, world, error));
    host.setViewport(100, 100);
    host.start();
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 20; ++frame) {
      if (frame % 2 == 0)
        input.mouseButtonsDown.insert("left");
      else
        input.mouseButtonsDown.clear();
      host.beginFrame(1.0F / 60.0F);
      host.update(1.0F / 60.0F);
    }
    assert(std::chrono::steady_clock::now() - started <
           std::chrono::seconds(2));
    execute(host, "assert(urgent_calls==10); assert(update_calls==20); "
                  "assert(not busy_task:done())");
    host.destroy();
    execute(host, "assert(busy_task:status()=='cancelled')");
  }
  std::filesystem::remove_all(root);
  std::cout << "HUD actions stay responsive during worker Lua tasks\n";
}
