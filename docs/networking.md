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

## Predicted authoritative movement

Prediction is opt-in and disabled when its explicit bounds are zero. A game
configures bounded input, snapshot, replay, and optional query history before
enabling a controller:

```lua
NetworkSession.configure({
  input_queue_capacity = 64,
  input_future_window = 32,
  input_head_of_line_timeout = 0.10,
  input_max_per_tick = 8,
  snapshot_buffer = 32,
  interpolation_delay = 0.10,
  extrapolation_limit = 0.05,
  prediction_history_limit = 64,
  prediction_visual_decay = 12.0,
})

NetworkSession.enable_prediction({
  network_id = player_network_id,
  input_message = "move_input",
  state = { x = 0.0, y = 0.0 },
  apply = function(state, input)
    return {
      x = state.x + input.x * input.dt,
      y = state.y + input.y * input.dt,
    }
  end,
})
```

`input_message` must be a declared owner-to-server `owned_entity` intent.
`predict_input` applies it immediately, assigns `seq`, stores the replayable
input, and sends it through the normal contract gateway. The authoritative
fixed tick consumes `take_inputs(network_id)` in sequence order and publishes
the resulting controller state with `publish_snapshot`. The client restores
that state immediately and deterministically replays only unacknowledged
inputs. `prediction_visual_offset` is render-only smoothing and must never be
fed back into simulation.

Capacity-rejected and timed-out inputs become ordered discard markers, so a
later snapshot acknowledgment removes them even if the immediate rejection is
lost. Duplicate/same-tick snapshots are ignored. Session epochs, ownership
generations, teleports, resets, disconnects, and despawns clear incompatible
history. `reset_prediction` is the explicit same-session scene rebase.

Non-owning clients use `remote_state(network_id)`, which interpolates a bounded
authoritative snapshot buffer and extrapolates only through the configured
limit. `prediction_diagnostics()` reports queue rejection/discard counts,
acknowledgments, replay depth, corrections, visual offsets, stale snapshots,
epoch/generation clears, interpolation, extrapolation, and clamps.

## Historical authoritative queries

The host may record a small selected set of collision circles for hitscan lag
compensation without rewinding the live world:

```lua
NetworkSession.configure({
  query_history_capacity = 32,
  query_history_max_entities = 16,
  query_history_rewind_ticks = 12,
})

NetworkSession.record_query_snapshot(server_tick, {
  { entity_id = target_id, layer = "players", x = x, y = y, radius = 0.45 },
})

local hit = NetworkSession.historical_raycast(
  client_tick, origin_x, origin_y, aim_x, aim_y, 24.0, "players", shooter_id
)
```

Snapshots are detached, sorted by stable entity ID, monotonically ticked, and
bounded by both count and entities per snapshot. Queries clamp to the declared
rewind window and report the actual `sampled_tick`. Invalid, duplicate, stale,
non-finite, or oversized snapshots are rejected atomically. This mechanism is
for selected server-side hit/visibility tests; it never grants authority and
never mutates Box2D, Jolt, or authored entities.

Call `clear_query_history()` before the authoritative simulation changes scene
or otherwise resets its tick space.

## Testing transport failures

`NetworkFaultSimulator` is a deterministic C++ test utility below the protocol
gateway. It can inject bounded loss, duplication, delay, jitter, and reordering
without opening sockets. Secure-session tests cover malformed/truncated packets,
oversized/deep payloads, schema violations, replay and rate limits, forged
ownership, stale epochs/generations, transfer/disconnect/reset, reconnect
expiry, queue capacity, and lifecycle ordering. The reference action-movement
test runs predicted input and authoritative correction through deterministic
loss, duplication, delay, reordering, and tick gaps, then verifies the final
authority state against the accepted input log.

## Legacy compatibility

Projects without `network_contract` may temporarily use `register_entity`,
`set_authority`, and `emit`. Contract projects reject those APIs so a game
cannot accidentally bypass its declared policy. New multiplayer examples must
use `spawn`, `transfer`, `send`, and contract-based replication.

This deprecation applies only to `NetworkSession.emit`, which sent an
undeclared generic event over the transport. `Events.emit` is the supported
in-process event bus for gameplay callbacks and does not use networking.
