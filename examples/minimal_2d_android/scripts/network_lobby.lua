local Lobby = {
  server_host = "127.0.0.1",
  tls_port = 39421,
  gameplay_port = 39420,
  trusted_certificate = "certs/server.crt",
  server_name = "localhost",
  connected = false,
  connecting = false,
  heartbeat = 0.0,
  connect_elapsed = 0.0,
  inbox = {},
}

function Lobby.configure(options)
  options = options or {}
  for key, value in pairs(options) do Lobby[key] = value end
end

function Lobby.connect()
  if Lobby.connected or Lobby.connecting then return true end
  Debug.log("TLS matchmaking connecting to " .. Lobby.server_host .. ":" .. tostring(Lobby.tls_port))
  Lobby.connecting = TlsClient.connect(
    Lobby.server_host, Lobby.tls_port,
    Lobby.trusted_certificate, Lobby.server_name
  )
  Lobby.connect_elapsed = 0.0
  return Lobby.connecting
end

local function send(kind, payload)
  if not Lobby.connected then return false end
  return TlsClient.send(Network.encode(kind, payload or {}))
end

function Lobby.list() return send("list_lobbies") end
function Lobby.create(name, private, password)
  return send("create_lobby", { name = name, private = private, password = password or "" })
end
function Lobby.join(id, password)
  return send("join_lobby", { lobby_id = id, password = password or "" })
end
function Lobby.leave() return send("leave_lobby") end

function Lobby.update(dt)
  Lobby.heartbeat = Lobby.heartbeat + dt
  if Lobby.connecting then
    Lobby.connect_elapsed = Lobby.connect_elapsed + dt
    if Lobby.connect_elapsed >= 8.0 then
      TlsClient.disconnect()
      Lobby.connecting = false
      Lobby.inbox[#Lobby.inbox + 1] = { type = "error", payload = { code = "tls_timeout", message = "MATCHMAKING TIMED OUT" } }
    end
  end
  for _, event in ipairs(TlsClient.events()) do
    if event.type == "connected" then
      Debug.log("TLS matchmaking connected")
      Lobby.connected = true
      Lobby.connecting = false
      Lobby.heartbeat = 0.0
      Lobby.list()
    elseif event.type == "disconnected" then
      Lobby.connected = false
      Lobby.connecting = false
      local error = TlsClient.error()
      Debug.log("TLS matchmaking disconnected: " .. (error ~= "" and error or "remote closed connection"))
      Lobby.inbox[#Lobby.inbox + 1] = { type = "error", payload = {
        code = "tls_disconnected",
        message = error ~= "" and ("MATCHMAKING: " .. error) or "MATCHMAKING DISCONNECTED",
      } }
    elseif event.type == "message" then
      local message = Network.decode(event.message)
      if message ~= nil then
        Debug.log("TLS matchmaking received " .. message.type)
        Lobby.inbox[#Lobby.inbox + 1] = message
      end
    end
  end
  if Lobby.connected and Lobby.heartbeat >= 15.0 then
    Lobby.heartbeat = 0.0
    send("heartbeat")
  end
end

function Lobby.events()
  local result = Lobby.inbox
  Lobby.inbox = {}
  return result
end

function Lobby.disconnect()
  TlsClient.disconnect()
  Lobby.connected = false
  Lobby.connecting = false
  Lobby.inbox = {}
end

return Lobby
