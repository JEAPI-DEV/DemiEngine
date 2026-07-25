# Game-Facing Networking

Networking is experimental and is enabled with `DEMI_ENABLE_NETWORK=ON`.
Games should use `NetworkSession`. The lower-level `Network` table exists for
transport tools and compatibility, but game scripts do not need ENet channels,
packet framing, or DTLS state.

## Session flow

```lua
if NetworkSession.host(39420) then
  NetworkSession.start_session({ scene_id = "scene://game", seed = 42 })
end

-- Or, from a client:
NetworkSession.connect("127.0.0.1", 39420)

local update = NetworkSession.process_events()
for _, event in ipairs(update.events) do
  if event.name == "player_ready" then
    Debug.log(event.sender_id .. " is ready")
  end
end
```

`process_events()` reports connection and session changes and returns named
game events. `diagnostics()` reports mode, local peer ID, security, latency,
connected peers, sent/received/rejected counts, and the latest useful error.

## Replicated entities and authority

Register a local entity once its peer identity has been assigned:

```lua
local id = "player_" .. NetworkSession.sender_id()
NetworkSession.register_entity("ent_player", { network_id = id })

function Player:on_update(dt)
  if NetworkSession.has_authority(id) then
    NetworkSession.update_entity(id, dt)
  end
end
```

Registration sends a reliable spawn. `update_entity` sends snapshots at the
configured interval, and remote transforms are extrapolated up to
`extrapolation_limit`. The authority owner is recorded for every network
entity. State and despawn messages from any other peer are rejected. Only the
host may call `set_authority`.

`despawn` is reliable and requires local authority. Named messages use
`NetworkSession.emit(name, payload, reliable)`.

## Replication safety

Component fields are not automatically network-visible. A field must carry
the `replicated` flag in component metadata; generated schemas expose this as
`x-demi-replicated`. The current allow-list is:

- `Transform2D`: position, rotation, scale
- `Transform3D`: position, rotation, scale
- `Rigidbody2D` and `Rigidbody3D`: velocity
- `Sprite`: flip state and color

Incoming snapshots are validated against this metadata before application.
Asset references, script properties, hierarchy parents, colliders, and other
authored state cannot be overwritten by a remote snapshot.

Transport and security remain isolated in `NetworkSystem`, while
`GameNetworkSession` owns identities, authority, and diagnostics. This keeps
game rules testable without opening sockets.
