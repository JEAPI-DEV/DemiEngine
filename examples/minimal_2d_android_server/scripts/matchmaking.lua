local Matchmaking = {}
Matchmaking.__index = Matchmaking

function Matchmaking.new(max_players)
  return setmetatable({
    max_players = max_players, clients = {}, lobbies = {}, names = {},
    tokens = {}, next_lobby_id = 1,
  }, Matchmaking)
end

function Matchmaking:send(client_id, kind, payload)
  TlsServer.send(client_id, Network.encode(kind, payload or {}))
end

function Matchmaking:member_count(lobby)
  local count = 0
  for _ in pairs(lobby.members) do count = count + 1 end
  return count
end

function Matchmaking:list(client_id)
  local result = {}
  for _, lobby in pairs(self.lobbies) do
    result[#result + 1] = {
      id = lobby.id, name = lobby.name, private = lobby.private,
      players = self:member_count(lobby), max_players = self.max_players,
    }
  end
  table.sort(result, function(a, b) return a.name < b.name end)
  self:send(client_id, "lobby_list", { lobbies = result })
end

function Matchmaking:leave(client_id)
  local client = self.clients[client_id]
  if client == nil or client.lobby_id == nil then return nil end
  local lobby = self.lobbies[client.lobby_id]
  client.lobby_id = nil
  if lobby == nil then return nil end
  if lobby.host_id == client_id then
    for member_id in pairs(lobby.members) do
      local member = self.clients[member_id]
      if member ~= nil then
        member.lobby_id = nil
        if member_id ~= client_id then self:send(member_id, "error", { code = "host_left", message = "Lobby host disconnected" }) end
      end
    end
    self.names[lobby.name] = nil
    self.lobbies[lobby.id] = nil
    return lobby.id
  end
  lobby.members[client_id] = nil
  return false
end

function Matchmaking:issue_token(client_id, lobby)
  local token = Crypto.random_token(32)
  self.tokens[token] = { client_id = client_id, lobby_id = lobby.id, expires = os.time() + 30 }
  return token
end

function Matchmaking:handle(client_id, text)
  local message = Network.decode(text)
  local client = self.clients[client_id]
  if message == nil or client == nil then return end
  Debug.log("TLS client " .. tostring(client_id) .. " -> " .. message.type)
  local payload = message.payload or {}
  client.last_seen = os.time()
  if client.command_window ~= client.last_seen then
    client.command_window = client.last_seen
    client.command_count = 0
  end
  client.command_count = (client.command_count or 0) + 1
  if client.command_count > 10 then
    self:send(client_id, "error", { code = "rate_limited", message = "Too many matchmaking requests" })
    return
  end

  if message.type == "list_lobbies" then
    self:list(client_id)
  elseif message.type == "heartbeat" then
    self:send(client_id, "heartbeat")
  elseif message.type == "leave_lobby" then
    self:leave(client_id)
  elseif message.type == "create_lobby" then
    local name = payload.name
    local private = payload.private == true
    local password = payload.password or ""
    if type(name) ~= "string" or #name < 1 or #name > 20 or name:match("^[a-z0-9_]+$") == nil then
      Debug.log("Rejected lobby name from TLS client " .. tostring(client_id))
      self:send(client_id, "error", { code = "invalid_name", message = "Use a-z, 0-9, _; max 20" })
    elseif self.names[name] ~= nil then
      self:send(client_id, "error", { code = "duplicate_name", message = "Lobby name already exists" })
    elseif private and (#password < 1 or #password > 64 or password:find("[%c]") ~= nil) then
      self:send(client_id, "error", { code = "invalid_password", message = "Password must be 1-64 printable characters" })
    else
      self:leave(client_id)
      local salt = private and Crypto.random_token(16) or ""
      local lobby = {
        id = self.next_lobby_id, name = name, private = private,
        salt = salt, password_hash = private and Crypto.password_hash(password, salt) or "",
        host_id = client_id, members = { [client_id] = true },
      }
      self.next_lobby_id = self.next_lobby_id + 1
      self.lobbies[lobby.id] = lobby
      self.names[name] = lobby.id
      client.lobby_id = lobby.id
      Debug.log("Created " .. (private and "private" or "public") .. " lobby " .. lobby.name)
      self:send(client_id, "lobby_created", { lobby_id = lobby.id, admission_token = self:issue_token(client_id, lobby) })
    end
  elseif message.type == "join_lobby" then
    local lobby = self.lobbies[tonumber(payload.lobby_id)]
    if lobby == nil then
      self:send(client_id, "error", { code = "not_found", message = "Lobby no longer exists" })
    elseif self:member_count(lobby) >= self.max_players then
      self:send(client_id, "error", { code = "full", message = "Lobby is full" })
    elseif lobby.private and not Crypto.secure_equals(Crypto.password_hash(payload.password or "", lobby.salt), lobby.password_hash) then
      Debug.log("Rejected private lobby password from TLS client " .. tostring(client_id))
      self:send(client_id, "error", { code = "wrong_password", message = "Incorrect password" })
      client.password_failures = (client.password_failures or 0) + 1
      if client.password_failures >= 5 then TlsServer.disconnect(client_id) end
    else
      client.password_failures = 0
      self:leave(client_id)
      lobby.members[client_id] = true
      client.lobby_id = lobby.id
      Debug.log("TLS client " .. tostring(client_id) .. " joined lobby " .. lobby.name)
      self:send(client_id, "lobby_joined", { lobby_id = lobby.id, admission_token = self:issue_token(client_id, lobby) })
    end
  end
end

function Matchmaking:update()
  local closed = {}
  for _, event in ipairs(TlsServer.events()) do
    if event.type == "connected" then
      self.clients[event.client_id] = { last_seen = os.time() }
      Debug.log("TLS matchmaking client connected: " .. tostring(event.client_id))
    elseif event.type == "disconnected" then
      Debug.log("TLS matchmaking client disconnected: " .. tostring(event.client_id))
      local lobby_id = self:leave(event.client_id)
      if lobby_id then closed[#closed + 1] = lobby_id end
      self.clients[event.client_id] = nil
    elseif event.type == "message" then
      self:handle(event.client_id, event.message)
    end
  end
  local now = os.time()
  for client_id, client in pairs(self.clients) do
    if now - client.last_seen > 45 then TlsServer.disconnect(client_id) end
  end
  for token, data in pairs(self.tokens) do
    if data.expires < now then self.tokens[token] = nil end
  end
  return closed
end

function Matchmaking:consume_token(token)
  local data = self.tokens[token or ""]
  self.tokens[token or ""] = nil
  if data == nil or data.expires < os.time() then return nil end
  local client = self.clients[data.client_id]
  local lobby = self.lobbies[data.lobby_id]
  if client == nil or lobby == nil or client.lobby_id ~= lobby.id then return nil end
  return data
end

return Matchmaking
