# Game-Facing Networking

Networking is experimental and is enabled with `DEMI_ENABLE_NETWORK=ON`.
Games use `NetworkSession`; the lower-level `Network` service is reserved for
transport tools. A multiplayer project should declare one validated network
contract so trust rules remain source data rather than Lua conventions.

```json
{
  "format_version": 1,
  "network_contract": "asset://network/arena_contract"
}
```

A `NetworkContract` asset declares server-spawned prefabs, ownership and
disconnect policy, replicated component fields, message direction, target,
reliability, rate/byte limits, and optional `DataSchema` payloads. The runtime
hashes the canonical contract and schemas; peers with incompatible hashes do
not enter the secure session.

## Secure session flow

```lua
if NetworkSession.host(39420) then
  NetworkSession.start_session({ scene_id = "scene://game", seed = 42 })
end

-- Or, from a client:
NetworkSession.connect("127.0.0.1", 39420)

local update = NetworkSession.process_events()
if NetworkSession.diagnostics().secure_ready then
  for _, event in ipairs(update.events) do
    if event.name == "player_ready" then
      Debug.log(event.sender_id .. " is ready")
    end
  end
end
```

`process_events()` returns only operations accepted by the bounded protocol
gateway. Claimed sender IDs are discarded; `event.sender_id` comes from the
authenticated transport peer. `diagnostics()` includes the lifecycle phase,
session epoch, contract hash, and bounded accepted/rejected security counters.

## Ownership and replicated entities

Only the authoritative host can create network IDs, spawn, transfer, revoke,
or despawn contract entities:

```lua
-- Host only. "player" is a replicated_prefabs key from the contract.
local network_id = NetworkSession.spawn("player", "ent_player", peer_id)

function Player:on_update(dt)
  if network_id ~= nil and NetworkSession.has_authority(network_id) then
    NetworkSession.update_entity(network_id, dt)
  end
end
```

`update_entity` captures only fields declared by the prefab contract and
marked `replicated` by component reflection. The gateway checks the session
epoch and current ownership generation, so delayed state from an old owner or
a despawned entity cannot overwrite current state. Reliable spawn state is
retained and replayed to late joiners before subsequent lifecycle operations.

The shared policy vocabulary is deliberately small:

- `server`, `owner`, and `all` identify message senders/receivers and field
  writers;
- `despawn`, `return_to_server`, and `transfer_by_game_policy` define what
  happens to a peer-owned entity on disconnect;
- `owned_entity`, `entity`, or `none` define message targets.

Ownership is identity, not permission. Each field and message has its own rule.
For a server-authoritative game, clients normally send declared input/intent
messages while the server writes gameplay state.

## Declared messages

```lua
NetworkSession.send("move_intent", player_network_id, {
  x = Input.action_value("move_x"),
  y = Input.action_value("move_y"),
})
```

Before Lua receives a message, the runtime validates its fixed header,
protocol version, contract hash, session epoch, sequence, byte size, JSON
structure, finite numbers, schema, authenticated sender, destination, target
ownership, ownership generation, and rate limit. Invalid traffic only changes
bounded diagnostic counters.

## Lifecycle and reconnect foundations

The engine lifecycle has explicit `closed`, `connected`, `authenticated`,
`ready`, `active`, and `reconnecting` phases. Session reset revokes ownership,
queued protocol state, counters, and reconnect leases before advancing the
epoch. Reconnect leases are single-use, expire, and are bound to the session
epoch plus every leased entity's ownership generation. A transfer, despawn, or
reset therefore invalidates an older lease.

Prediction, reconciliation, interpolation, and lag compensation are separate
latency-hiding features planned after this secure authority foundation. An
owner-writable transform works for prototypes but is intentionally a weaker
trust boundary than server-validated intent.

## Testing transport failures

`NetworkFaultSimulator` is a deterministic C++ test utility below the protocol
gateway. It can inject bounded loss, duplication, delay, and reordering without
opening sockets. Secure-session tests cover malformed/truncated packets,
oversized/deep payloads, schema violations, replay and rate limits, forged
ownership, stale epochs/generations, transfer/disconnect/reset, reconnect
expiry, queue capacity, and lifecycle ordering.

## Legacy compatibility

Projects without `network_contract` may temporarily use `register_entity`,
`set_authority`, and `emit`. Contract projects reject those APIs so a game
cannot accidentally bypass its declared policy. New multiplayer examples must
use `spawn`, `transfer`, `send`, and contract-based replication.

This deprecation applies only to `NetworkSession.emit`, which sent an
undeclared generic event over the transport. `Events.emit` is the supported
in-process event bus for gameplay callbacks and does not use networking.
