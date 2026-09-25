# Legacy APIs

These APIs have been removed. Migrate callers to their replacements; there
are no compatibility aliases.

| Legacy API | Replacement | Reason |
|---|---|---|
| `Network.http_get`, `Network.http_post_form` | Asynchronous `demi.network.http` requests | HTTP no longer blocks gameplay; HTTPS verifies certificates and hostnames. Collect the response from its request handle. |
| `Network.lobby_*` | Game-owned REST requests through `demi.network.http` | Lobby payloads and endpoints are application policy, not native transport behavior. |
| `NetworkSession.emit(topic, ...)` | `NetworkSession.send` for network messages, `Events.emit` for local gameplay events | `emit` sent an undeclared generic event over the transport, bypassing the network contract's declared message list. |
| `NetworkSession.register_entity(...)` | `NetworkSession.spawn(...)` with a declared contract prefab | The server allocates identity and enforces declared ownership policy. |
| `NetworkSession.set_authority(...)` | `NetworkSession.transfer(...)` | Contract-based ownership generations replace manual authority assignment. |
| `NetworkSession.register_claim_once`, `apply_claim_once`, `try_claim_once`, `request_claim_once_sync`, `reset_claims` | Declared messages and game-owned collection rules | Generic coin/claim policy no longer lives in native session bindings. Custom transport probes own their protocol through `demi.network`. |
| `NetworkSession.set_local_color(...)` | Replicated `Sprite.color` or game-owned avatar data | The setter no longer contributed to contract replication. |

`register_entity`, `set_authority`, and `emit` are absent from the native
module, including projects without a `network_contract`. Their former generic
event, entity-spawn/state/despawn, and authority-change wire messages are
rejected. See [networking](networking.md#removed-pre-contract-apis).

## Lua imports and stubs

| Legacy | Replacement | Reason |
|---|---|---|
| Implicit engine globals (e.g. `Input`, `Hud`) | `local Input = require("demi.input")` | Globals fail at runtime; every script declares its imports. There is no compatibility facade. |
| `Transform` service | `demi.transform2d` / `demi.transform3d` | Split into per-dimension modules. |
| `demi.network_session` | `demi.network.session` | Flat paths became dotted domain namespaces, without aliases. |
| `demi.tls_client` | `demi.network.tls.client` | Same namespace migration. |
| `demi.tls_server` | `demi.network.tls.server` | Same namespace migration. |
| `demi.audio_source` | `demi.audio.source` | Same namespace migration. |
| `demi.rigidbody3d` | `demi.physics.rigidbody3d` | Same namespace migration. |
| Single-file `demi.lua` stub library | Per-service stubs under `scripts/stubs/demi/`, exported with `demi lua-stubs generate` | Old annotations falsely suggest global APIs remain available to LuaLS. |

See [explicit Lua engine imports](lua-modules.md) for the current module
layout.
