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

  const std::filesystem::path projectDirectory = std::filesystem::temp_directory_path() / "demi_snapshot_replication_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts", error);
  if (error) {
    std::cerr << "Failed to create snapshot replication test project directory.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "probe.lua", R"lua(
local Probe = {}

local function assert_true(condition, message)
  if not condition then
    error(message, 2)
  end
end

local function install_network_mock(options)
  local sent = {}
  local events = {}
  Network = {
    available = function()
      return true
    end,
    is_host = function()
      return options.is_host
    end,
    is_connected = function()
      return options.is_connected ~= false
    end,
    sender_id = function(assigned)
      if options.is_host then
        return "host"
      end
      return assigned or "client"
    end,
    send = function(message, _reliable, peer_id, channel)
      sent[#sent + 1] = {
        message = message,
        peer_id = peer_id or 0,
        channel = channel or 0,
      }
      return true
    end,
    encode = function(message_type, payload)
      return { type = message_type, payload = payload }
    end,
    decode = function(message)
      return message
    end,
    events = function()
      local current = events
      events = {}
      return current
    end,
  }
  return {
    sent = sent,
    push_event = function(event)
      events[#events + 1] = event
    end,
  }
end

local function install_entity_mock()
  local destroyed = {}
  Entity = {
    create = function(id)
      destroyed[id] = false
      return true
    end,
    destroy = function(id)
      destroyed[id] = true
      return true
    end,
    set_position = function()
      return true
    end,
  }
  return destroyed
end

local function find_sent(sent, message_type)
  for _, entry in ipairs(sent) do
    if entry.message ~= nil and entry.message.type == message_type then
      return entry
    end
  end
  return nil
end

function Probe:on_start()
  install_entity_mock()
  local host_network = install_network_mock({ is_host = true })
  package.loaded["demi.net.snapshot_replication"] = nil
  local SnapshotReplication = require("demi.net.snapshot_replication")

  local host = SnapshotReplication.new({})
  host:register_claim_once("coin_1")
  assert_true(host:apply_claim_once("coin_1", "host", false), "host failed to seed claimed coin")

  host_network.push_event({ type = "connected", peer_id = 7 })
  host:process_events()
  local sync = find_sent(host_network.sent, "claim_once_sync")
  assert_true(sync ~= nil, "host did not send claim_once_sync to reconnecting peer")
  assert_true(sync.peer_id == 7, "claim_once_sync was not targeted at the reconnecting peer")
  assert_true(sync.message.payload.claims[1].object_id == "coin_1", "claim_once_sync omitted claimed coin id")
  assert_true(sync.message.payload.claims[1].collector_id == "host", "claim_once_sync omitted collector id")

  local destroyed = install_entity_mock()
  local client_network = install_network_mock({ is_host = false })
  local client = SnapshotReplication.new({})
  assert_true(client:request_claim_once_sync(), "client did not send claim_once_sync_request")
  local request = find_sent(client_network.sent, "claim_once_sync_request")
  assert_true(request ~= nil, "claim_once_sync_request was not sent")

  client_network.push_event({
    type = "message",
    message = { type = "claim_once_sync", payload = { claims = { ["1"] = { object_id = "coin_1", collector_id = "host" } } } },
  })
  client:process_events()
  local registered = client:register_claim_once("coin_1", {
    on_removed = function(object_id)
      Entity.destroy(object_id)
    end,
  })
  assert_true(not registered, "synced claimed coin was still registered as collectible after rejoin")
  assert_true(destroyed.coin_1 == true, "synced claimed coin was not removed when registered after sync")

  destroyed.coin_3 = false
  client:register_claim_once("coin_3", {
    on_removed = function(object_id)
      Entity.destroy(object_id)
    end,
  })
  client_network.push_event({
    type = "message",
    message = { type = "claim_once_sync", payload = { claims = { ["1"] = { object_id = "coin_3", collector_id = "peer4" } } } },
  })
  client:process_events()
  assert_true(destroyed.coin_3 == true, "claim sync after registration did not remove ghost coin")
  assert_true(client.claim_once_objects.coin_3.claimed, "claim sync after registration did not mark object claimed")

  client:register_claim_once("coin_2")
  assert_true(client:try_claim_once("coin_2", { x = 100.0, y = 100.0 }), "client failed to send claim request")
  assert_true(client.claim_once_objects.coin_2.pending, "client claim did not enter pending state")
  client_network.push_event({
    type = "message",
    message = { type = "claim_once_rejected", payload = { object_id = "coin_2", collector_id = "peer7" } },
  })
  client:process_events()
  assert_true(not client.claim_once_objects.coin_2.pending, "claim rejection did not clear pending state")

  NetworkSession.configure({ port = 40000 })
  local engine_removed = false
  local engine_claimed_local = false
  NetworkSession.register_claim_once("coin_4", {
    can_claim = function(object_id, collector_id, claim)
      return object_id == "coin_4" and collector_id == NetworkSession.sender_id() and claim.x == 1.0 and claim.y == 2.0
    end,
    on_removed = function(object_id)
      engine_removed = object_id == "coin_4"
    end,
    on_claimed_local = function(object_id)
      engine_claimed_local = object_id == "coin_4"
    end,
  })
  assert_true(NetworkSession.apply_claim_once("coin_4", NetworkSession.sender_id(), true, { x = 1.0, y = 2.0 }), "engine NetworkSession did not apply valid local claim")
  assert_true(engine_removed, "engine NetworkSession did not call on_removed")
  assert_true(engine_claimed_local, "engine NetworkSession did not call on_claimed_local")
  assert_true(not NetworkSession.try_claim_once("coin_4", { x = 1.0, y = 2.0 }), "claimed object should not be claimable before reset")
  NetworkSession.reset_claims()
  assert_true(NetworkSession.register_claim_once("coin_4"), "claim reset should allow scene-reloaded objects to register as unclaimed")

  Save.set_string("test", "snapshot_replication", "passed")
end

return Probe
)lua")) {
    std::cerr << "Failed to write snapshot replication probe.lua.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";

  runtime::World world;
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

  host.start();
  if (host.saveString("test", "snapshot_replication") != "passed") {
    std::cerr << "Snapshot replication Lua regression tests did not pass.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
