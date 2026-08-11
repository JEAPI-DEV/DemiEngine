local Lobby = require("demi.network.lobby")

Test.case("lobby state is deterministic and readiness is explicit", function()
  local sent = {}
  local lobby = Lobby.new({ send = function(name, target, data)
    sent[#sent + 1] = { name = name, target = target, data = data }
    return true
  end }, { map = "arena", required = 2 })
  lobby:add("peer2", { team = "red" })
  lobby:add("peer1", { team = "blue" })
  Test.equal(lobby:all_ready(), false)
  Test.equal(lobby:handle({ name = "lobby_ready", sender_id = "peer1",
    data = { ready = true } }, true), true)
  Test.equal(lobby:handle({ name = "lobby_ready", sender_id = "peer2",
    data = { ready = true } }, true), true)
  Test.equal(lobby:all_ready(), true)
  local snapshot = lobby:snapshot()
  Test.equal(snapshot.peers[1].id, "peer1")
  Test.equal(snapshot.peers[2].id, "peer2")
  Test.equal(#sent, 2)
end)

Test.case("clients cannot author authoritative lobby state", function()
  local lobby = Lobby.new({ send = function() return true end })
  lobby:add("peer1")
  Test.equal(lobby:handle({ name = "lobby_ready", sender_id = "peer1",
    data = { ready = true } }, false), false)
  Test.equal(lobby.peers.peer1.ready, false)
  Test.equal(lobby:handle({ name = "lobby_ready", sender_id = "forged",
    data = { ready = true } }, true), false)
end)
