local Matchmaking = require("matchmaking")
local Server = {}

local GAME_PORT = 39420
local TLS_PORT = 39421
local MAX_PLAYERS = 4
local SESSION_SCENE = "scene://minimal_2d_android/platformer"
local CERTIFICATE = os.getenv("DEMI_DTLS_CERT") or "certs/server.crt"
local PRIVATE_KEY = os.getenv("DEMI_DTLS_KEY") or "certs/server.key"

local matchmaking = Matchmaking.new(MAX_PLAYERS)
local peers = {}
local claims = {}
local next_peer_number = 2

local function send(peer_id, kind, payload, reliable)
  Network.send(Network.encode(kind, payload or {}), reliable ~= false, peer_id or 0, reliable == false and 1 or 0)
end

local function claim_list()
  local result = {}
  for object_id, collector_id in pairs(claims) do result[#result + 1] = { object_id = object_id, collector_id = collector_id } end
  table.sort(result, function(a, b) return a.object_id < b.object_id end)
  return result
end

local function authenticate(peer_id, token)
  local admission = matchmaking:consume_token(token)
  if admission == nil then return false end
  local sender_id = next(peers) == nil and "host" or "peer" .. tostring(next_peer_number)
  if sender_id ~= "host" then next_peer_number = next_peer_number + 1 end
  peers[peer_id] = { sender_id = sender_id, client_id = admission.client_id, lobby_id = admission.lobby_id }
  send(peer_id, "assign_peer", { peer_id = sender_id })
  send(peer_id, "session_start", { scene_id = SESSION_SCENE, level = 1, seed = 1001, server_authoritative = true })
  send(peer_id, "claim_once_sync", { claims = claim_list() })
  return true
end

local function on_message(peer_id, text)
  local message = Network.decode(text)
  if message == nil then return end
  if peers[peer_id] == nil then
    if message.type ~= "lobby_auth" or not authenticate(peer_id, (message.payload or {}).token) then Network.disconnect_peer(peer_id) end
    return
  end
  local sender_id = peers[peer_id].sender_id
  local payload = message.payload or {}
  if message.type == "transform_snapshot" then
    payload.sender_id = sender_id
    send(0, message.type, payload, false)
  elseif message.type == "claim_once_request" then
    local object_id = payload.object_id
    if object_id ~= nil and object_id ~= "" and claims[object_id] == nil then
      claims[object_id] = sender_id
      payload.collector_id = sender_id
      send(0, "claim_once_claimed", payload)
    else
      send(peer_id, "claim_once_rejected", { object_id = object_id or "", collector_id = sender_id })
    end
  elseif message.type == "claim_once_sync_request" then
    send(peer_id, "claim_once_sync", { claims = claim_list() })
  end
end

local function close_gameplay_lobby(lobby_id)
  for peer_id, peer in pairs(peers) do
    if peer.lobby_id == lobby_id then Network.disconnect_peer(peer_id); peers[peer_id] = nil end
  end
end

function Server:on_start()
  if not Network.host_dtls(GAME_PORT, CERTIFICATE, PRIVATE_KEY, 128) then error("Failed to start DTLS server: " .. Network.security_error()) end
  if not TlsServer.listen(TLS_PORT, CERTIFICATE, PRIVATE_KEY, 128) then error("Failed to start TLS server: " .. TlsServer.error()) end
  Debug.log("TLS matchmaking TCP " .. TLS_PORT .. " / DTLS gameplay UDP " .. GAME_PORT)
end

function Server:on_update(_dt)
  for _, lobby_id in ipairs(matchmaking:update()) do close_gameplay_lobby(lobby_id) end
  for peer_id, peer in pairs(peers) do
    local client = matchmaking.clients[peer.client_id]
    if client == nil or client.lobby_id ~= peer.lobby_id then
      Network.disconnect_peer(peer_id)
      peers[peer_id] = nil
    end
  end
  for _, event in ipairs(Network.events()) do
    if event.type == "disconnected" then peers[event.peer_id] = nil
    elseif event.type == "message" then on_message(event.peer_id, event.message) end
  end
end

return Server
