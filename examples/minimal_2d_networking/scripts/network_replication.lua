local SnapshotReplication = require("demi.net.snapshot_replication")

local session = SnapshotReplication.new({
  port = 39420,
  send_interval = 1.0 / 60.0,
  extrapolation_limit = 0.10,
  remote_prefab = {
    name = "Network Ghost",
    texture = "asset://sprites/player",
    layer = "network",
    scale = { 0.8, 0.8 },
  },
})

return {
  host = function(port)
    return session:host(port)
  end,
  connect = function(address, port)
    return session:connect(address, port)
  end,
  disconnect = function()
    session:disconnect()
  end,
  is_connected = function()
    return Network.is_connected()
  end,
  process_events = function()
    return session:process_events()
  end,
  register_claim_once = function(id, options)
    return session:register_claim_once(id, options)
  end,
  try_claim_once = function(id)
    return session:try_claim_once(id)
  end,
  update_local_transform = function(entity_id, dt)
    session:update_entity(entity_id, dt)
  end,
}
