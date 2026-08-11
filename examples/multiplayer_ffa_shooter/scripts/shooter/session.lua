local Config = require("shooter.config")

local Session = {}

NetworkSession.configure({
  port = Config.port,
  max_peers = 12,
  send_interval = 1.0 / 30.0,
  extrapolation_limit = 0.12,
  remote_prefab = {
    name = "Remote Fighter",
    shape = "circle",
    layer = "actors",
    sorting_order = 9,
    size = {0.9, 0.9},
    color = {0.44, 0.65, 1.0, 1.0},
  },
})

function Session.host()
  if not NetworkSession.host(Config.port) then
    return false
  end
  NetworkSession.start_session({
    mode = "free_for_all",
    scene_id = "scene://multiplayer_ffa_shooter/arena",
    format_version = 1,
  })
  return true
end

function Session.connect(address)
  return NetworkSession.connect(address, Config.port)
end

function Session.register_local(entity_id, sender_id, color, mode)
  NetworkSession.set_local_color(color[1], color[2], color[3], color[4])
  if mode == "host" then
    return NetworkSession.spawn("player", entity_id, "server")
  end
  return true
end

function Session.update_local(sender_id, dt)
  local network_id = NetworkSession.network_id_for_owner(
    sender_id == "host" and "server" or sender_id)
  return network_id ~= nil and NetworkSession.update_entity(network_id, dt)
end

function Session.spawn_remote(sender_id)
  if not NetworkSession.is_host() then return nil end
  if NetworkSession.network_id_for_owner(sender_id) ~= nil then return nil end
  return NetworkSession.spawn("player", Config.player_entity, sender_id)
end

return Session
