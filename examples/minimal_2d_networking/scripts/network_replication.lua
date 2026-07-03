NetworkSession.configure({
  port = 39420,
  send_interval = 1.0 / 60.0,
  extrapolation_limit = 0.10,
  remote_prefab = {
    name = "Network Ghost",
    texture = "asset://sprites/player",
    layer = "network",
    scale = { 0.8, 0.8 },
    color = { 0.30, 0.62, 1.0, 1.0 },
  },
})

return NetworkSession
