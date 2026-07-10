local Client = {
  elapsed = 0.0,
  tls_ready = false,
  admission_token = nil,
  auth_sent = false,
  claim_requested = false,
  claim_confirmed = false,
}

function Client:on_start()
  NetworkSession.configure({
    trusted_certificate = os.getenv("DEMI_DTLS_CA") or "",
    server_name = os.getenv("DEMI_DTLS_SERVER_NAME") or "localhost",
  })
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
        if not NetworkSession.connect("127.0.0.1", 39420) then error("test client could not start DTLS connection") end
      end
    end
  end
  local events = NetworkSession.process_events()
  if events.connected and self.admission_token ~= nil and not self.auth_sent then
    self.auth_sent = Network.send(Network.encode("lobby_auth", { token = self.admission_token }), true)
  end
  local session = events.session or NetworkSession.current_session()
  if session ~= nil and session.scene_id ~= nil and not self.claim_requested then
    self.claim_requested = true
    NetworkSession.register_claim_once("test_coin", {
      on_removed = function()
        self.claim_confirmed = true
      end,
    })
    NetworkSession.try_claim_once("test_coin", { x = 0.0, y = 0.0 })
  end
  if self.claim_confirmed then
    Debug.log("LUA_SERVER_HANDSHAKE_OK")
    Debug.log("LUA_SERVER_CLAIM_OK")
    Runtime.quit()
    return
  end
  if events.disconnected or self.elapsed >= 3.0 then
    Debug.log("LUA_SERVER_HANDSHAKE_FAILED")
    Runtime.quit()
  end
end

return Client
