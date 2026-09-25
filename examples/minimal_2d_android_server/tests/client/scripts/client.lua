local Debug = require("demi.debug")
local Application = require("demi.application")
local Network = require("demi.network")
local TlsClient = require("demi.network.tls.client")

local Client = {
  elapsed = 0.0,
  tls_ready = false,
  admission_token = nil,
  auth_sent = false,
  claim_requested = false,
  claim_confirmed = false,
}

function Client:on_start()
  if not TlsClient.connect("127.0.0.1", 39421, os.getenv("DEMI_DTLS_CA") or "", os.getenv("DEMI_DTLS_SERVER_NAME") or "localhost") then
    error("test client could not start TLS matchmaking")
  end
end

function Client:on_update(dt)
  self.elapsed = self.elapsed + dt
  for _, event in ipairs(TlsClient.events()) do
    if event.type == "connected" then
      self.tls_ready = true
      TlsClient.send(Network.encode("create_lobby", { name = "INVALID!", private = false }))
    elseif event.type == "message" then
      local message = Network.decode(event.message)
      if message ~= nil and message.type == "error" and message.payload.code == "invalid_name" then
        TlsClient.send(Network.encode("create_lobby", { name = "integration_test", private = true, password = "secret" }))
      elseif message ~= nil and message.type == "lobby_created" then
        self.admission_token = message.payload.admission_token
        if not Network.connect_dtls("127.0.0.1", 39420,
            os.getenv("DEMI_DTLS_CA") or "", os.getenv("DEMI_DTLS_SERVER_NAME") or "localhost") then
          error("test client could not start DTLS connection")
        end
      end
    end
  end
  local disconnected = false
  for _, event in ipairs(Network.events()) do
    if event.type == "connected" and self.admission_token ~= nil and not self.auth_sent then
      self.auth_sent = Network.send(Network.encode("lobby_auth", {token = self.admission_token}), true)
    elseif event.type == "disconnected" then
      disconnected = true
    elseif event.type == "message" then
      local message = Network.decode(event.message)
      if message and message.type == "session_start" and not self.claim_requested then
        self.claim_requested = Network.send(Network.encode("claim_once_request",
          {object_id = "test_coin", x = 0.0, y = 0.0}), true)
      elseif message and message.type == "claim_once_claimed"
          and message.payload.object_id == "test_coin" then
        self.claim_confirmed = true
      end
    end
  end
  if self.claim_confirmed then
    Debug.log("LUA_SERVER_HANDSHAKE_OK")
    Debug.log("LUA_SERVER_CLAIM_OK")
    Application.quit()
    return
  end
  if disconnected or self.elapsed >= 3.0 then
    Debug.log("LUA_SERVER_HANDSHAKE_FAILED")
    Application.quit()
  end
end

return Client
