-- Client for this game's dedicated lobby server. Coin rules and wire names
-- belong to the game, independently of contract-based engine replication.
local Network = require("demi.network")
local Entity = require("demi.entity")
local Transform = require("demi.transform2d")
local Body = require("demi.physics.rigidbody2d")
local Sprite = require("demi.sprite2d")

local Replication = {}
local peer_id = "client"
local session = nil
local coins, claimed, pending, remotes = {}, {}, {}, {}
local accumulator = 0.0

local function send(kind, payload, reliable)
  return Network.send(Network.encode(kind, payload), reliable ~= false)
end

local function finish_claim(id, collector)
  if type(id) ~= "string" or type(collector) ~= "string" or claimed[id] then return end
  claimed[id] = collector
  pending[id] = nil
  local coin = coins[id]
  if coin then
    coin.on_removed(id)
    if collector == peer_id then coin.on_claimed_local() end
  end
end

function Replication.sender_id() return peer_id end
function Replication.current_session() return session end

function Replication.disconnect()
  Network.disconnect()
  for _, remote in pairs(remotes) do Entity.destroy(remote.id) end
  peer_id, session = "client", nil
  coins, claimed, pending, remotes = {}, {}, {}, {}
  accumulator = 0.0
end

function Replication.connect(address, port, certificate, server_name)
  Replication.disconnect()
  return Network.connect_dtls(address, port, certificate, server_name)
end

function Replication.reset_coins()
  coins, claimed, pending = {}, {}, {}
end

function Replication.register_coin(id, callbacks)
  coins[id] = callbacks
  if claimed[id] then callbacks.on_removed(id) end
end

function Replication.collect_coin(id, position)
  local coin = coins[id]
  if not coin or claimed[id] or pending[id] then return false end
  if not Network.is_connected() then
    if not coin.can_claim(id, peer_id, position) then return false end
    finish_claim(id, peer_id)
    return true
  end
  if not send("claim_once_request", {object_id = id, x = position.x, y = position.y}) then
    return false
  end
  pending[id] = true
  return true
end

function Replication.sync_coins()
  if Network.is_connected() then return send("claim_once_sync_request", {}) end
  return false
end

function Replication.remote_position(sender)
  local remote = remotes[sender]
  if remote then return remote.x, remote.y end
end

function Replication.update_avatar(entity_id, color, dt)
  for _, remote in pairs(remotes) do
    remote.age = math.min(remote.age + dt, 0.1)
    Transform.set_position(remote.id, remote.x + remote.vx * remote.age,
      remote.y + remote.vy * remote.age)
  end
  if not Network.is_connected() then return end
  accumulator = accumulator + dt
  if accumulator < 1.0 / 60.0 then return end
  accumulator = 0.0
  local x, y = Transform.get_position(entity_id)
  if x == nil then return end
  local vx, vy = Body.get_velocity(entity_id)
  send("transform_snapshot", {entity_id = entity_id, x = x, y = y,
    vx = vx or 0.0, vy = vy or 0.0, color = color}, false)
end

local function apply_avatar(payload)
  local sender = payload.sender_id
  if type(sender) ~= "string" or sender == peer_id
      or type(payload.x) ~= "number" or type(payload.y) ~= "number" then return end
  local remote = remotes[sender]
  if not remote or not Entity.exists(remote.id) then
    local id = "remote_" .. sender
    if not Entity.create(id, {name = "Remote Player", components = {
      Transform2D = {position = {payload.x, payload.y}, scale = {0.8, 0.8}},
      Sprite = {texture = "asset://sprites/player", layer = "network"},
    }}) then return end
    remote = {id = id}
    remotes[sender] = remote
  end
  remote.x, remote.y = payload.x, payload.y
  remote.vx = type(payload.vx) == "number" and payload.vx or 0.0
  remote.vy = type(payload.vy) == "number" and payload.vy or 0.0
  remote.age = 0.0
  Transform.set_position(remote.id, remote.x, remote.y)
  local color = payload.color
  if type(color) == "table" and #color == 4 then
    Sprite.set_color(remote.id, color[1], color[2], color[3], color[4])
  end
end

function Replication.process_events()
  local result = {connected = false, disconnected = false, session_started = false}
  for _, event in ipairs(Network.events()) do
    if event.type == "connected" then
      result.connected = true
    elseif event.type == "disconnected" then
      Replication.disconnect()
      result.disconnected = true
    elseif event.type == "message" then
      local message = Network.decode(event.message)
      local payload = message and message.payload
      if type(payload) == "table" then
        if message.type == "assign_peer" and type(payload.peer_id) == "string" then
          peer_id = payload.peer_id
        elseif message.type == "session_start" then
          session = payload
          result.session, result.session_started = payload, true
        elseif message.type == "transform_snapshot" then
          apply_avatar(payload)
        elseif message.type == "claim_once_claimed" then
          finish_claim(payload.object_id, payload.collector_id)
        elseif message.type == "claim_once_rejected" then
          if payload.object_id then pending[payload.object_id] = nil end
        elseif message.type == "claim_once_sync" then
          for _, claim in ipairs(payload.claims or {}) do
            finish_claim(claim.object_id, claim.collector_id)
          end
        end
      end
    end
  end
  return result
end

return Replication
