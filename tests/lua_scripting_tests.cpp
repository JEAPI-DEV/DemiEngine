#include "demi/runtime/LuaScriptHost.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool writeFile(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << contents;
  return true;
}

} // namespace

int main() {
  namespace runtime = demi::runtime;

  const std::filesystem::path projectDirectory = std::filesystem::temp_directory_path() / "demi_lua_scripting_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts", error);
  if (error) {
    std::cerr << "Failed to create Lua test project directory.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "probe.lua", R"lua(
local Probe = {}
function Probe:on_start()
  Save.write("profile", { name = "Ada", coins = 7 }, 1)
  Save.register_migration(1, 2, function(state)
    state.level = 3
    return state
  end)
  local profile = Save.read("profile")
  if profile and profile.name == "Ada" and profile.coins == 7 and profile.level == 3 and Save.version("profile") == 2 then
    Save.set_string("test", "profile", "migrated")
  end
  Cutscene.play("cutscene://intro")
  if Cutscene.is_playing() and Cutscene.active() == "cutscene://intro" then
    Save.set_string("test", "cutscene", "playing")
  end
  Cutscene.pause()
  if not Cutscene.is_playing() then
    Save.set_string("test", "cutscene_paused", "true")
  end
  Cutscene.resume()
  Cutscene.skip()
  Runtime.set_max_fps(144)
  if Runtime.get_max_fps() == 144 then
    Save.set_number("test", "max_fps", 144)
  end
end
function Probe:on_update(dt)
  if Input.is_down("space") then
    Save.set_string("test", "space", "down")
  else
    Save.set_string("test", "space", "up")
  end
end
return Probe
)lua")) {
    std::cerr << "Failed to write probe.lua.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "button.lua", R"lua(
local Button = {}
function Button:on_ui_click(event)
  Save.set_string("test", "clicked", event.id)
end
return Button
)lua")) {
    std::cerr << "Failed to write button.lua.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";

  runtime::World world;
  world.hudCanvasSize = runtime::Vec2{.x = 100.0F, .y = 100.0F};
  world.hudButtons.push_back(runtime::HudButtonElement{
    .id = "button_start",
    .label = "START",
    .position = runtime::Vec2{.x = 0.0F, .y = 0.0F},
    .size = runtime::Vec2{.x = 100.0F, .y = 100.0F},
    .script = "script://scripts/button.lua",
  });

  runtime::InputState input;
  input.mousePosition = runtime::Vec2{.x = 50.0F, .y = 50.0F};

  runtime::LuaScriptHost host;
  std::string luaError;
  if (!host.initialize(world, input, nullptr, luaError)) {
    std::cerr << "Lua host failed to initialize: " << luaError << '\n';
    return 1;
  }
  if (!host.loadWorldScripts(project, world, luaError)) {
    std::cerr << "Lua scripts failed to load: " << luaError << '\n';
    return 1;
  }

  host.setViewport(100, 100);
  host.start();
  if (host.saveString("test", "profile") != "migrated") {
    std::cerr << "Save.read/write migration hook did not migrate profile data.\n";
    return 1;
  }
  if (host.saveString("test", "cutscene") != "playing" || host.saveString("test", "cutscene_paused") != "true") {
    std::cerr << "Cutscene Lua API did not report expected state transitions.\n";
    return 1;
  }
  if (host.saveNumber("test", "max_fps").value_or(0.0F) != 144.0F) {
    std::cerr << "Runtime max FPS Lua API did not persist expected value.\n";
    return 1;
  }
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "space") != "up") {
    std::cerr << "Input.is_down returned true for an unpressed key.\n";
    return 1;
  }

  input.mouseButtonsDown.insert("left");
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "clicked") != "button_start") {
    std::cerr << "HUD button click did not reach Lua on_ui_click.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
