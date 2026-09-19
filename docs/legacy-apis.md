# Legacy APIs

These public APIs still work but are deprecated. New code should use their
replacements.

| Legacy API | Replacement | Reason |
|---|---|---|
| `NetworkSession.emit(topic, ...)` | `NetworkSession.send` for network messages, `Events.emit` for local gameplay events | `emit` sent an undeclared generic event over the transport, bypassing the network contract's declared message list. |
| `NetworkSession.register_entity(...)` | `NetworkSession.spawn(...)` with a declared contract prefab | Contract-based projects reject `register_entity` so declared ownership policy cannot be bypassed. |
| `NetworkSession.set_authority(...)` | `NetworkSession.transfer(...)` | Contract-based ownership generations replace manual authority assignment. |

Contract-based projects reject `register_entity`, `set_authority`, and `emit`.
Projects without a `network_contract` may keep using them temporarily; see
[networking](networking.md#legacy-compatibility).

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
