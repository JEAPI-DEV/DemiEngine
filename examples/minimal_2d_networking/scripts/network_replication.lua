local Replication = {
  send_interval = 1.0 / 60.0,
  accumulator = 0.0,
  local_peer_id = nil,
  ghosts = {},
}

local SNAPSHOT_CHANNEL = 1
local EXTRAPOLATION_LIMIT = 0.10

local function local_sender_id()
  if Network.is_host() then
    return "host"
  end
  return Replication.local_peer_id or "client"
end

local function encode_assign(peer_id)
  return "assign|peer" .. tostring(peer_id)
end

local function decode_assign(message)
  return string.match(message, "^assign|([^|]+)$")
end

local function encode_transform(sender_id, entity_id, x, y, vx, vy)
  return string.format("transform|%s|%s|%.4f|%.4f|%.4f|%.4f", sender_id, entity_id, x, y, vx, vy)
end

local function decode_transform(message)
  local sender_id, entity_id, x, y, vx, vy = string.match(message, "^transform|([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)$")
  if sender_id == nil then
    return nil
  end
  return sender_id, entity_id, tonumber(x), tonumber(y), tonumber(vx), tonumber(vy)
end

local function ensure_ghost(ghost_id, x, y)
  if Replication.ghosts[ghost_id] then
    return
  end

  Entity.create(ghost_id, {
    name = "Network Ghost",
    components = {
      Transform2D = {
        position = { x, y },
        rotation = 0.0,
        scale = { 0.8, 0.8 },
      },
      Sprite = {
        texture = "asset://sprites/player",
        layer = "network",
      },
    },
  })
  Replication.ghosts[ghost_id] = {
    x = x,
    y = y,
    vx = 0.0,
    vy = 0.0,
    age = 0.0,
  }
end

local function apply_transform(sender_id, entity_id, x, y, vx, vy)
  if sender_id == local_sender_id() then
    return
  end

  local ghost_id = "net_" .. sender_id .. "_" .. entity_id
  ensure_ghost(ghost_id, x, y)
  local ghost = Replication.ghosts[ghost_id]
  ghost.x = x
  ghost.y = y
  ghost.vx = vx or 0.0
  ghost.vy = vy or 0.0
  ghost.age = 0.0
  Entity.set_position(ghost_id, ghost.x + ghost.vx * 0.025, ghost.y + ghost.vy * 0.025)
end

local function update_ghosts(dt)
  for ghost_id, ghost in pairs(Replication.ghosts) do
    ghost.age = math.min(ghost.age + dt, EXTRAPOLATION_LIMIT)
    Entity.set_position(ghost_id, ghost.x + ghost.vx * ghost.age, ghost.y + ghost.vy * ghost.age)
  end
end

function Replication.host(port)
  if not Network.available() then
    Debug.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to host.")
    return false
  end
  Replication.local_peer_id = nil
  return Network.host(port or 39420, 8)
end

function Replication.connect(address, port)
  if not Network.available() then
    Debug.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to connect.")
    return false
  end
  Replication.local_peer_id = nil
  return Network.connect(address or "127.0.0.1", port or 39420)
end

function Replication.update_local_transform(entity_id, dt)
  if not Network.available() then
    return
  end

  for _, event in ipairs(Network.events()) do
    if event.type == "connected" then
      Debug.log("Network peer connected: " .. tostring(event.peer_id))
      if Network.is_host() then
        Network.send(encode_assign(event.peer_id), true, event.peer_id, 0)
      end
    elseif event.type == "disconnected" then
      Debug.log("Network peer disconnected: " .. tostring(event.peer_id))
    elseif event.type == "message" then
      local assigned_peer_id = decode_assign(event.message)
      if assigned_peer_id ~= nil then
        Replication.local_peer_id = assigned_peer_id
      else
        local sender_id, source_entity, x, y, vx, vy = decode_transform(event.message)
        if sender_id ~= nil and x ~= nil and y ~= nil then
          if Network.is_host() and sender_id ~= "host" then
            sender_id = "peer" .. tostring(event.peer_id)
            Network.send(encode_transform(sender_id, source_entity, x, y, vx or 0.0, vy or 0.0), false, 0, SNAPSHOT_CHANNEL)
          end
          apply_transform(sender_id, source_entity, x, y, vx or 0.0, vy or 0.0)
        end
      end
    end
  end

  update_ghosts(dt)

  if not Network.is_host() and not Network.is_connected() then
    return
  end

  Replication.accumulator = Replication.accumulator + dt
  if Replication.accumulator < Replication.send_interval then
    return
  end
  Replication.accumulator = 0.0

  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return
  end

  local vx, vy = Rigidbody2D.get_velocity(entity_id)
  Network.send(encode_transform(local_sender_id(), entity_id, x, y, vx or 0.0, vy or 0.0), false, 0, SNAPSHOT_CHANNEL)
end

return Replication
