#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool writeFile(const std::filesystem::path &path, const char *contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output)
    return false;
  output << contents;
  return true;
}

} // namespace

int main() {
  namespace runtime = demi::runtime;

  const std::filesystem::path projectDirectory =
      std::filesystem::temp_directory_path() /
      "demi_network_session_lua_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts", error);
  if (error ||
      !writeFile(projectDirectory / "scripts" / "probe.lua", R"lua(
local Probe = {}

local function assert_true(condition, message)
  if not condition then
    error(message, 2)
  end
end

function Probe:on_start()
  NetworkSession.configure({ port = 40000 })

  local removed = false
  local claimed_local = false
  assert_true(NetworkSession.register_claim_once("coin", {
    can_claim = function(object_id, collector_id, claim)
      return object_id == "coin"
        and collector_id == NetworkSession.sender_id()
        and claim.x == 1.0
        and claim.y == 2.0
    end,
    on_removed = function(object_id)
      removed = object_id == "coin"
    end,
    on_claimed_local = function(object_id)
      claimed_local = object_id == "coin"
    end,
  }), "claim-once registration failed")
  assert_true(NetworkSession.apply_claim_once(
    "coin", NetworkSession.sender_id(), true, { x = 1.0, y = 2.0 }
  ), "valid local claim was rejected")
  assert_true(removed and claimed_local, "claim callbacks were not invoked")
  assert_true(not NetworkSession.try_claim_once("coin", { x = 1.0, y = 2.0 }),
    "claimed object remained claimable")
  NetworkSession.reset_claims()
  assert_true(NetworkSession.register_claim_once("coin"),
    "claim reset did not restore registration")

  assert_true(NetworkSession.register_entity("player", {
    network_id = "player_client",
  }), "replicated entity registration failed")
  assert_true(NetworkSession.owner("player_client") == "client",
    "entity owner was not recorded")
  assert_true(NetworkSession.has_authority("player_client"),
    "local entity did not grant local authority")
  assert_true(NetworkSession.update_entity("player_client", 1.0),
    "offline entity update should be a no-op success")

  local diagnostics = NetworkSession.diagnostics()
  assert_true(diagnostics.mode == "offline", "wrong offline diagnostics mode")
  assert_true(diagnostics.local_peer_id == "client",
    "diagnostics omitted local peer identity")
  Save.set_string("test", "network_session", "passed")
end

return Probe
)lua")) {
    std::cerr << "Failed to create network session Lua test fixture.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";

  runtime::World world;
  runtime::Entity player;
  player.id = "player";
  player.setComponent(runtime::Transform2DComponent{
      .position = {.x = 1.0F, .y = 2.0F},
  });
  world.entities.push_back(std::move(player));

  runtime::InputState input;
  runtime::LuaScriptHost host;
  std::string luaError;
  if (!host.initialize(world, input, nullptr, luaError) ||
      !host.loadWorldScripts(project, world, luaError)) {
    std::cerr << "Lua host setup failed: " << luaError << '\n';
    return 1;
  }

  host.start();
  if (host.saveString("test", "network_session") != "passed") {
    std::cerr << "Game-facing NetworkSession Lua test did not pass.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
