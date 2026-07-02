#include "demi/runtime/LuaScriptHost.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

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
  require("action_module")
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
  Events.subscribe("hud_action", function(event)
    Save.set_string("test", "hud_action", event.action .. ":" .. event.id)
  end)
  if Hud.set_button_label("button_start", "GO") then
    Save.set_string("test", "button_label", "updated")
  end
  Runtime.set_max_fps(144)
  if Runtime.get_max_fps() == 144 then
    Save.set_number("test", "max_fps", 144)
  end
end
-- @HandleAction("test.annotated")
function Probe:handle_annotated_action(event)
  Save.set_string("test", "annotated_action", event.action .. ":" .. event.id)
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

  if (!writeFile(projectDirectory / "scripts" / "action_module.lua", R"lua(
local ActionModule = {}
-- @HandleAction("test.module")
function ActionModule.handle_action(event)
  Save.set_string("test", "module_action", event.action .. ":" .. event.id)
end
return ActionModule
)lua")) {
    std::cerr << "Failed to write action_module.lua.\n";
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

  if (!writeFile(projectDirectory / "scripts" / "prop_probe.lua", R"lua(
local PropProbe = {}
function PropProbe:on_start()
  if self.entity_id == "ent_prop" and self.speed == 12.5 and self.enabled == true and self.tags[1] == "runner" and self.spawn.x == 3.0 then
    Save.set_string("test", "script_properties", "generic")
  end
end
return PropProbe
)lua")) {
    std::cerr << "Failed to write prop_probe.lua.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";
  project.scriptModules = {"action_module"};

  runtime::World world;
  world.hudCanvasSize = runtime::Vec2{.x = 100.0F, .y = 100.0F};
  runtime::Entity propEntity;
  propEntity.id = "ent_prop";
  propEntity.name = "Property Probe";
  propEntity.luaScript = runtime::LuaScriptComponent{
    .module = "script://scripts/prop_probe.lua",
    .propertiesJson = R"json({ "enabled": true, "speed": 12.5, "tags": ["runner"], "spawn": { "x": 3.0, "y": 4.0 } })json",
  };
  world.entities.push_back(std::move(propEntity));
  world.hudButtons.push_back(runtime::HudButtonElement{
    .id = "button_start",
    .label = "START",
    .position = runtime::Vec2{.x = 0.0F, .y = 0.0F},
    .size = runtime::Vec2{.x = 45.0F, .y = 100.0F},
    .script = "script://scripts/button.lua",
    .action = "test.annotated",
  });
  world.hudButtons.push_back(runtime::HudButtonElement{
    .id = "button_module",
    .label = "MODULE",
    .position = runtime::Vec2{.x = 55.0F, .y = 0.0F},
    .size = runtime::Vec2{.x = 45.0F, .y = 100.0F},
    .action = "test.module",
  });

  runtime::InputState input;
  input.mousePosition = runtime::Vec2{.x = 25.0F, .y = 50.0F};

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
  if (host.saveString("test", "script_properties") != "generic") {
    std::cerr << "Generic LuaScript properties were not copied into Lua script fields.\n";
    return 1;
  }
  if (host.saveString("test", "button_label") != "updated" || world.hudButtons[0].label != "GO") {
    std::cerr << "Hud.set_button_label did not update the HUD button label.\n";
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
  if (host.saveString("test", "hud_action") != "test.annotated:button_start") {
    std::cerr << "HUD button action annotation did not emit hud_action.\n";
    return 1;
  }
  if (host.saveString("test", "annotated_action") != "test.annotated:button_start") {
    std::cerr << "@HandleAction did not dispatch to annotated Lua function.\n";
    return 1;
  }

  input.mouseButtonsDown.clear();
  host.update(1.0F / 60.0F);
  input.mousePosition = runtime::Vec2{.x = 75.0F, .y = 50.0F};
  input.mouseButtonsDown.insert("left");
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "module_action") != "test.module:button_module") {
    std::cerr << "Project-listed Lua module @HandleAction did not dispatch.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
