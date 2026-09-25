#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

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
      std::filesystem::temp_directory_path() / "demi_network_session_lua_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts", error);
  if (error || !writeFile(projectDirectory / "scripts" / "probe.lua", R"lua(
local Save = require("demi.save")
local NetworkSession = require("demi.network.session")

local Probe = {}

local function assert_true(condition, message)
  if not condition then
    error(message, 2)
  end
end

function Probe:on_start()
  NetworkSession.configure({ port = 40000 })

  assert_true(NetworkSession.register_claim_once == nil
    and NetworkSession.apply_claim_once == nil and NetworkSession.try_claim_once == nil
    and NetworkSession.request_claim_once_sync == nil and NetworkSession.reset_claims == nil
    and NetworkSession.set_local_color == nil, "non-contract helpers remain exposed")
  assert_true(not NetworkSession.host() and not NetworkSession.connect(),
    "session transport started without a contract")

  assert_true(NetworkSession.register_entity == nil
    and NetworkSession.set_authority == nil and NetworkSession.emit == nil,
    "retired pre-contract APIs remain exposed")
  assert_true(NetworkSession.spawn("player", "player") == nil,
    "entity spawn bypassed the required contract")
  assert_true(not NetworkSession.transfer("player_client", "client"),
    "ownership transfer bypassed the required contract")
  assert_true(not NetworkSession.send("undeclared", nil, {}),
    "undeclared network message was accepted")
  assert_true(NetworkSession.owner("player_client") == nil
    and not NetworkSession.has_authority("player_client"),
    "an unknown entity granted authority")

  assert_true(not NetworkSession.enable_prediction({
    network_id = "player_client",
    state = { x = 0.0 },
    apply = function(state, input)
      return { x = state.x + input.x }
    end,
  }), "prediction unexpectedly invented a history default")
  NetworkSession.configure({
    port = 40000,
    prediction_history_limit = 8,
  })
  assert_true(not NetworkSession.enable_prediction({
    network_id = "player_client",
    state = { x = 0.0 },
    apply = function(state, input) return { x = state.x + input.x } end,
  }), "offline prediction bypassed contract ownership")
  assert_true(NetworkSession.predict_input("player_client", { x = 2.0 }) == nil,
    "unknown entity accepted predicted input")
  assert_true(NetworkSession.prediction_state("player_client") == nil,
    "unknown entity retained prediction state")
  local query_diagnostics = NetworkSession.query_history_diagnostics()
  assert_true(query_diagnostics.depth == 0 and query_diagnostics.latest_tick == 0,
    "query history diagnostics were not installed")
  assert_true(not NetworkSession.record_query_snapshot(1, {}),
    "offline peers unexpectedly recorded authoritative query history")
  assert_true(NetworkSession.historical_raycast(
    1, 0, 0, 1, 0, 10, "players") == nil,
    "offline peers unexpectedly queried authoritative history")

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
