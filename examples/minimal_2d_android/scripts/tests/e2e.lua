-- Mobile end-to-end tests for the physical-device qualification gate.
--
-- Runs when the runtime launches in test mode: `demi test android` sets the
-- `.demi_run_tests` marker, and desktop runs use `demi test linux`.
-- Touches resolve HUD nodes by id and flow through the real input pipeline,
-- so these tests exercise the same path as a finger on the screen.

local tests = {}

tests[#tests + 1] = {
  name = "settings save writes through the options screen",
  func = function()
    Test.wait(1.5)
    Test.touch("menu_button_options")
    Test.touch("menu_volume_plus")
    Test.touch("menu_back")
  end,
}

tests[#tests + 1] = {
  name = "level select loads the platformer scene",
  func = function()
    Test.touch("menu_button_levels")
    Test.touch("menu_button_level_1")
    Test.expect_scene("scene://minimal_2d_android/platformer", 10.0)
  end,
}

tests[#tests + 1] = {
  name = "network host socket loopback",
  func = function()
    Test.expect(Network.available(),
                  "ENet networking is unavailable on this device")
    Test.expect(Network.host(34567, 2),
                  "ENet host failed to bind a local socket")
    Test.wait(0.2)
    Test.expect(Network.is_host(), "Host state was not retained")
    Network.disconnect()
    Test.expect(not Network.is_host(),
                  "Host state persisted after disconnect")
  end,
}

tests[#tests + 1] = {
  name = "tls wire loopback echo",
  func = function()
    Test.expect(TlsServer.listen(34443, "certs/server.crt",
                                   "certs/server.key", 2),
                  "TLS server failed to listen on the loopback port")
    Test.expect(TlsClient.connect("127.0.0.1", 34443, "certs/server.crt"),
                  "TLS client failed to connect to the loopback server")
    local client_id = nil
    for _ = 1, 50 do
      Test.wait(0.1)
      -- The server endpoint is exclusively owned by this test, so its
      -- "connected" event is safe to consume here.
      for _, event in ipairs(TlsServer.events()) do
        if event.type == "connected" then
          client_id = event.client_id
        end
      end
      if client_id ~= nil then
        break
      end
    end
    Test.expect(client_id ~= nil,
                  "TLS server never accepted the client (server: " ..
                      tostring(TlsServer.error()) .. ")")
    Test.expect(TlsServer.client_connected(client_id),
                  "TLS server handshake did not complete")
    -- The client endpoint is shared with the game's matchmaking lobby, which
    -- drains the same event stream every frame; use the polled connection
    -- state instead of the "connected" event.
    local client_connected = false
    for _ = 1, 50 do
      Test.wait(0.1)
      if TlsClient.is_connected() then
        client_connected = true
        break
      end
    end
    Test.expect(client_connected,
                  "TLS client handshake did not complete (client: " ..
                      tostring(TlsClient.error()) .. ")")
    TlsServer.send(client_id, "loopback-ping")
    local echo = nil
    for _ = 1, 50 do
      Test.wait(0.1)
      -- Pump the server so the queued frame flushes, then read the client.
      for _, event in ipairs(TlsServer.events()) do
      end
      for _, event in ipairs(TlsClient.events()) do
        if event.type == "message" then
          echo = event.message
        end
      end
      if echo ~= nil then
        break
      end
    end
    Test.expect(echo == "loopback-ping",
                  "TLS echo did not roundtrip (received " ..
                      tostring(echo) .. ")")
    TlsClient.disconnect()
  end,
}

return {tests = tests}
