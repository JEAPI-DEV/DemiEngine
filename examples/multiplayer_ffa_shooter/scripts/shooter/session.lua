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

function Session.register_local(entity_id, sender_id, color)
  NetworkSession.set_local_color(color[1], color[2], color[3], color[4])
  return NetworkSession.register_entity(entity_id, {
    network_id = "fighter_" .. sender_id,
    owner = sender_id,
  })
end

function Session.update_local(sender_id, dt)
  return NetworkSession.update_entity("fighter_" .. sender_id, dt)
end

return Session
