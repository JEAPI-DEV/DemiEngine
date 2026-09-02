#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

using namespace demi::runtime;

bool writeFile(const std::filesystem::path &path, const char *contents) {
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
  const std::filesystem::path projectDirectory =
      std::filesystem::temp_directory_path() / "demi_lua_mobile_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts" / "tests",
                                      error);

#if defined(_WIN32)
  _putenv_s("XDG_DATA_HOME",
            (projectDirectory / "storage" / "data").string().c_str());
#else
  setenv("XDG_DATA_HOME",
         (projectDirectory / "storage" / "data").string().c_str(), 1);
#endif

  if (!writeFile(projectDirectory / "scripts" / "action_module.lua", R"lua(
local ActionModule = {}
-- @HandleAction("mobile_probe_action")
function ActionModule.handle_action(event)
  Save.set_string("test", "mobile_action_fired", event.action .. ":" .. event.id)
end
return ActionModule
)lua")) {
    std::cerr << "Failed to write action_module.lua.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "tests" / "mobile.lua",
                 R"lua(
return {tests = {
  {name = "tap reaches button by node id", func = function()
    Mobile.wait(0.2)
    Mobile.touch("probe_button")
    Mobile.wait(0.2)
  end},
  {name = "node center and expect pass", func = function()
    Mobile.wait(0.1)
    Mobile.expect(Mobile.node_center("probe_button") ~= nil,
                  "probe_button center was nil")
  end},
  {name = "scene timeout fails the test", func = function()
    Mobile.expect_scene("scene://never/loads", 0.05)
  end},
}}
)lua")) {
    std::cerr << "Failed to write tests/mobile.lua.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";
  project.scriptModules = {"action_module"};
  if (!writeFile(projectDirectory / "scripts" / "probe.lua",
                 "local Probe = {}\n"
                 "Probe.action_module = require(\"action_module\")\n"
                 "function Probe:on_update(dt) end\nreturn Probe\n")) {
    std::cerr << "Failed to write probe.lua.\n";
    return 1;
  }

  runtime::World world;
  world.activeSceneId = "scene://probe/menu";
  runtime::ui::UiNode probeButton;
  probeButton.id = "probe_button";
  probeButton.type = "button";
  probeButton.action = "mobile_probe_action";
  probeButton.focusable = true;
  probeButton.layout.position = runtime::Vec2{.x = 100.0F, .y = 100.0F};
  probeButton.layout.size = runtime::Vec2{.x = 200.0F, .y = 60.0F};
  world.ui.nodes.push_back(probeButton);
  runtime::ui::UiLayoutEngine{}.layout(world.ui, world.ui.canvasSize);

  runtime::InputState input;
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
  host.setViewport(960, 540);

  if (!host.mobileNodeCenterCanvas("probe_button")) {
    std::cerr << "Mobile node resolution failed for probe_button.\n";
    return 1;
  }
  if (host.mobileNodeCenterCanvas("missing_button")) {
    std::cerr << "Mobile node resolution accepted an unknown node.\n";
    return 1;
  }

  host.startMobileTests("tests.mobile");
  if (!host.mobileTestsActive()) {
    std::cerr << "Mobile test harness did not start.\n";
    return 1;
  }

  for (int frame = 0; frame < 600 && host.mobileTestsActive(); ++frame) {
    host.drainSyntheticTouches(input);
    host.updateMobileTests(1.0 / 60.0);
    host.update(1.0 / 60.0);
    if (!input.touches.empty())
      std::cerr << "frame " << frame << " touches=" << input.touches.size()
                << " phase="
                << (input.touches.front().phase ==
                            runtime::TouchPhase::Began
                        ? "began"
                        : input.touches.front().phase ==
                                runtime::TouchPhase::Ended
                            ? "ended"
                            : "other")
                << " captured="
                << (world.ui.pointerCaptures.contains(0x54455354LL)
                        ? world.ui.pointerCaptures.at(0x54455354LL)
                        : std::string("none"))
                << "\n";
  }

  if (host.mobileTestsActive()) {
    std::cerr << "Mobile tests did not finish within the frame budget.\n";
    return 1;
  }
  if (host.mobileTestsPassed() != 2 || host.mobileTestsFailed() != 1) {
    std::cerr << "Mobile test summary expected 2 passed / 1 failed, got "
              << host.mobileTestsPassed() << " passed / "
              << host.mobileTestsFailed() << " failed.\n";
    return 1;
  }
  if (host.saveString("test", "mobile_action_fired") !=
      "mobile_probe_action:probe_button") {
    std::cerr << "Mobile.touch did not activate the HUD button action.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
