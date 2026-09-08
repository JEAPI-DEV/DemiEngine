local Config = require("shooter.config")

local Session = {
  controllers = {},
  entities = {},
  owners = {},
  prediction_enabled = {},
  server_tick = 0,
  local_tick = 0,
}

NetworkSession.configure({
  port = Config.port,
  max_peers = 12,
  send_interval = 1.0 / 30.0,
  extrapolation_limit = 0.05,
  initial_prediction = 0.0,
  interpolation_delay = 0.10,
  snapshot_buffer = 32,
  input_queue_capacity = 64,
  input_future_window = 64,
  input_head_of_line_timeout = 0.10,
  input_max_per_tick = 8,
  prediction_history_limit = 64,
  prediction_visual_decay = 12.0,
  query_history_capacity = 32,
  query_history_max_entities = 16,
  query_history_rewind_ticks = 12,
  remote_prefab = {
    name = "Remote Fighter",
    shape = "circle",
    layer = "actors",
    sorting_order = 9,
    size = {0.9, 0.9},
    color = {0.44, 0.65, 1.0, 1.0},
  },
})

local function normalized(x, y)
  local length = math.sqrt(x * x + y * y)
  if length > 1.0 then
    return x / length, y / length
  end
  return x, y
end

local function apply_input(state, input)
  local dx, dy = normalized(input.dx or 0.0, input.dy or 0.0)
  local dt = math.max(0.0, math.min(input.dt or 0.0, 0.05))
  local x = state.x + dx * Config.player_speed * dt
  local y = state.y + dy * Config.player_speed * dt
  return {
    x = math.max(-Config.arena_half_width, math.min(x, Config.arena_half_width)),
    y = math.max(-Config.arena_half_height, math.min(y, Config.arena_half_height)),
    vx = dx * Config.player_speed,
    vy = dy * Config.player_speed,
  }
end

local function state_for(entity_id)
  local x, y = Transform.get_position(entity_id)
  return {x = x or 0.0, y = y or 0.0, vx = 0.0, vy = 0.0}
end

local function authority_entity_id(owner)
  return "ent_authority_" .. owner:gsub("[^%w_]", "_")
end

function Session.reset()
  Session.controllers = {}
  Session.entities = {}
  Session.owners = {}
  Session.prediction_enabled = {}
  Session.server_tick = 0
  Session.local_tick = 0
end

function Session.host()
  Session.reset()
  if not NetworkSession.host(Config.port) then
    return false
  end
  NetworkSession.clear_query_history()
  NetworkSession.start_session({
    mode = "free_for_all",
    scene_id = "scene://multiplayer_ffa_shooter/arena",
    format_version = 1,
  })
  return true
end

function Session.connect(address)
  Session.reset()
  return NetworkSession.connect(address, Config.port)
end

function Session.register_local(entity_id, sender_id, color, mode)
  NetworkSession.set_local_color(color[1], color[2], color[3], color[4])
  if mode ~= "host" then
    return true
  end
  local network_id = NetworkSession.spawn("player", entity_id, "server")
  if network_id == nil then
    return false
  end
  Session.controllers[network_id] = state_for(entity_id)
  Session.entities[network_id] = entity_id
  Session.owners[network_id] = "server"
  return true
end

function Session.enable_local_prediction(sender_id)
  local network_id = NetworkSession.network_id_for_owner(sender_id)
  if network_id == nil then
    return nil
  end
  if Session.prediction_enabled[network_id] then
    return network_id
  end
  if not NetworkSession.bind_local_entity(network_id, Config.player_entity) then
    return nil
  end
  if not NetworkSession.enable_prediction({
    network_id = network_id,
    input_message = "move_input",
    state = state_for(Config.player_entity),
    apply = apply_input,
  }) then
    return nil
  end
  Session.prediction_enabled[network_id] = true
  return network_id
end

function Session.predict_local(sender_id, dx, dy, dt)
  local network_id = Session.enable_local_prediction(sender_id)
  if network_id == nil then
    return false
  end
  Session.local_tick = Session.local_tick + 1
  if NetworkSession.predict_input(network_id, {
    dx = dx,
    dy = dy,
    dt = math.min(dt, 0.05),
    tick = Session.local_tick,
  }) == nil then
    return false
  end
  local state = NetworkSession.prediction_state(network_id)
  if state == nil then
    return false
  end
  Rigidbody2D.set_velocity(Config.player_entity, 0.0, 0.0)
  Transform.set_position(Config.player_entity, state.x, state.y)
  return true
end

function Session.spawn_remote(sender_id)
  if not NetworkSession.is_host() then return nil end
  if NetworkSession.network_id_for_owner(sender_id) ~= nil then return nil end
  local entity_id = authority_entity_id(sender_id)
  local x, y = Config.spawn_for(sender_id, 0)
  if not Entity.spawn(entity_id, {
    prefab = "prefab://player",
    position = {x, y},
  }) then
    return nil
  end
  local color = Config.color_for(sender_id)
  Sprite2D.set_color(entity_id, color[1], color[2], color[3], color[4])
  local network_id = NetworkSession.spawn("player", entity_id, sender_id)
  if network_id == nil then
    Entity.destroy(entity_id)
    return nil
  end
  Session.controllers[network_id] = state_for(entity_id)
  Session.entities[network_id] = entity_id
  Session.owners[network_id] = sender_id
  return network_id
end

function Session.step_authority()
  if not NetworkSession.is_host() then
    return
  end
  Session.server_tick = Session.server_tick + 1
  local circles = {}
  for network_id, state in pairs(Session.controllers) do
    local entity_id = Session.entities[network_id]
    if Session.owners[network_id] == "server" then
      state = state_for(entity_id)
    else
      for _, input in ipairs(NetworkSession.take_inputs(network_id)) do
        state = apply_input(state, input)
      end
      Transform.set_position(entity_id, state.x, state.y)
      Rigidbody2D.set_velocity(entity_id, 0.0, 0.0)
    end
    Session.controllers[network_id] = state
    NetworkSession.publish_snapshot(network_id, state)
    circles[#circles + 1] = {
      entity_id = network_id,
      layer = "players",
      x = state.x,
      y = state.y,
      radius = Config.shot_radius,
    }
  end
  NetworkSession.record_query_snapshot(Session.server_tick, circles)
end

function Session.historical_hit(shooter_id, data)
  if not NetworkSession.is_host() or data == nil then
    return nil
  end
  local owner = shooter_id == "host" and "server" or shooter_id
  local shooter_network_id = NetworkSession.network_id_for_owner(owner)
  local hit = NetworkSession.historical_raycast(
    data.tick or Session.server_tick,
    data.x,
    data.y,
    data.dx,
    data.dy,
    data.distance or Config.shot_range,
    "players",
    shooter_network_id
  )
  return hit ~= nil and NetworkSession.owner(hit.entity_id) or nil
end

function Session.current_tick()
  return NetworkSession.is_host() and Session.server_tick or Session.local_tick
end

return Session
