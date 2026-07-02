NetworkSession.configure({
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

return NetworkSession
