local Replication = {
  send_interval = 0.05,
  accumulator = 0.0,
  ghosts = {},
}

local function encode_transform(entity_id, x, y)
  return string.format("transform|%s|%.4f|%.4f", entity_id, x, y)
end

local function decode_transform(message)
  local entity_id, x, y = string.match(message, "^transform|([^|]+)|([^|]+)|([^|]+)$")
  if entity_id == nil then
    return nil
  end
  return entity_id, tonumber(x), tonumber(y)
end

function Replication.host(port)
  if not Network.available() then
    Debug.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to host.")
    return false
  end
  return Network.host(port or 39420, 8)
end

function Replication.connect(address, port)
  if not Network.available() then
    Debug.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to connect.")
    return false
  end
  return Network.connect(address or "127.0.0.1", port or 39420)
end

function Replication.update_local_transform(entity_id, dt)
  if not Network.available() then
    return
  end

  for _, event in ipairs(Network.events()) do
    if event.type == "connected" then
      Debug.log("Network peer connected: " .. tostring(event.peer_id))
    elseif event.type == "disconnected" then
      Debug.log("Network peer disconnected: " .. tostring(event.peer_id))
    elseif event.type == "message" then
      local source_entity, x, y = decode_transform(event.message)
      if source_entity ~= nil and x ~= nil and y ~= nil then
        local ghost_id = "net_peer_" .. tostring(event.peer_id) .. "_" .. source_entity
        if not Replication.ghosts[ghost_id] then
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
          Replication.ghosts[ghost_id] = true
        else
          Entity.set_position(ghost_id, x, y)
        end
      end
    end
  end

  if not Network.is_host() and not Network.is_connected() then
    return
  end

  Replication.accumulator = Replication.accumulator + dt
  if Replication.accumulator < Replication.send_interval then
    return
  end
  Replication.accumulator = 0.0

  local x, y = Entity.get_position(entity_id)
  if x ~= nil and y ~= nil then
    Network.send(encode_transform(entity_id, x, y), false)
  end
end

return Replication
