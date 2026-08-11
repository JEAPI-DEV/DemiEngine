local Lobby = {}
Lobby.__index = Lobby

local function copy(value)
  local result = {}
  for key, item in pairs(value or {}) do result[key] = item end
  return result
end

function Lobby.new(adapter, options)
  assert(type(adapter) == "table" and type(adapter.send) == "function")
  options = options or {}
  return setmetatable({
    adapter = adapter,
    peers = {},
    map = options.map,
    required = options.required or 1,
    names = {
      ready = options.ready_message or "lobby_ready",
      team = options.team_message or "lobby_team",
      map = options.map_message or "lobby_map",
      state = options.state_message or "lobby_state",
    },
  }, Lobby)
end

function Lobby:add(peer_id, options)
  assert(type(peer_id) == "string" and peer_id ~= "")
  options = options or {}
  self.peers[peer_id] = { ready = false, team = options.team }
end

function Lobby:remove(peer_id) self.peers[peer_id] = nil end

function Lobby:snapshot()
  local ids, peers = {}, {}
  for id in pairs(self.peers) do ids[#ids + 1] = id end
  table.sort(ids)
  for _, id in ipairs(ids) do
    local peer = self.peers[id]
    peers[#peers + 1] = { id = id, ready = peer.ready, team = peer.team }
  end
  return { map = self.map, required = self.required, peers = peers }
end

function Lobby:all_ready()
  local count = 0
  for _, peer in pairs(self.peers) do
    count = count + 1
    if not peer.ready then return false end
  end
  return count >= self.required
end

function Lobby:broadcast()
  return self.adapter.send(self.names.state, nil, self:snapshot())
end

function Lobby:request_ready(value)
  return self.adapter.send(self.names.ready, nil, { ready = value == true })
end

function Lobby:request_team(team)
  assert(team == nil or type(team) == "string")
  return self.adapter.send(self.names.team, nil, { team = team })
end

function Lobby:select_map(map)
  assert(type(map) == "string" and map ~= "")
  return self.adapter.send(self.names.map, nil, { map = map })
end

function Lobby:handle(event, authoritative)
  if type(event) ~= "table" or type(event.name) ~= "string" then return false end
  local data, sender = event.data or {}, event.sender_id
  if event.name == self.names.state then
    if authoritative or type(data.peers) ~= "table" then return false end
    self.map, self.required, self.peers = data.map, data.required or 1, {}
    for _, peer in ipairs(data.peers) do
      if type(peer.id) == "string" then self.peers[peer.id] = copy(peer) end
    end
    return true
  end
  if not authoritative or type(sender) ~= "string" or not self.peers[sender] then
    return false
  end
  if event.name == self.names.ready and type(data.ready) == "boolean" then
    self.peers[sender].ready = data.ready
  elseif event.name == self.names.team and
      (data.team == nil or type(data.team) == "string") then
    self.peers[sender].team = data.team
  elseif event.name == self.names.map and type(data.map) == "string" then
    self.map = data.map
  else
    return false
  end
  self:broadcast()
  return true
end

return Lobby
