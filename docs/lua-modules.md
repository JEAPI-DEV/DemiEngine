# Explicit Lua engine imports

Engine services are modules. Every script declares what it uses, including
scripts loaded by another script and package modules:

```lua
local Input = require("demi.input")
local Transform3D = require("demi.transform3d")

local Player = {}
function Player:on_update(dt)
  if Input.down("forward") then
    Transform3D.add_position(self.entity_id, 0, 0, -4 * dt)
  end
end
return Player
```

Import only the services you use. Aliases are local choices; there is no
`require("demi")` import-all facade. Existing method names and arguments are
unchanged. Requiring a module does not create a global for other scripts.

Related services share a dotted domain namespace. Individual leaf names use
snake_case where needed and keep dimensional suffixes together:

| Domain | Modules |
| --- | --- |
| Networking | `demi.network`, `demi.network.session`, `demi.network.tls.client`, `demi.network.tls.server`, `demi.network.crypto` |
| Audio | `demi.audio`, `demi.audio.source` |
| Physics | `demi.physics`, `demi.physics.query2d`, `demi.physics.query3d`, `demi.physics.rigidbody2d`, `demi.physics.rigidbody3d`, `demi.physics.character_controller3d`, `demi.physics.destruction3d` |
| Meshes | `demi.mesh.procedural`, `demi.mesh.deformation` |
| Math | `demi.math.scalar`, `demi.math.vector2`, `demi.math.vector3`, `demi.math.random` |
| Voxels | `demi.voxel.world` |

Standalone services such as `demi.input`, `demi.entity`, and `demi.hud` retain
their direct paths. Namespace nesting does not implicitly import parent or child
modules: `require("demi.network")` is the low-level network API, not an aggregate
of its submodules. For example, import sessions with
`local NetworkSession = require("demi.network.session")`.

The former `Transform` service is `demi.transform2d`; the other transform module
is `demi.transform3d`. The complete native module inventory is the files under
`scripts/stubs/demi/` recursively, excluding underscore-prefixed type-only files.

The native host supplies these modules through Lua's standard preload mechanism.
`require` caches their tables per Lua state. Native bindings are still registered
at host initialization; the change makes dependencies explicit only, leaving
startup timing and permission behavior unchanged. Ordinary Lua helpers such as
`demi.script`, `demi.ui`, and installed package modules still use the existing
package resolver. Package unit tests and runtime E2E tests both explicitly import
`demi.test`; their test-context-specific helper APIs remain distinct.

## Autocomplete and types

Run `demi lua-stubs generate` to export the modular annotation library into
`.demi/lua/demi/`, including reflected component types. Project templates include
this layout and configure LuaLS `workspace.library` for `.demi/lua`.

Do not execute annotation files or add the stub directory to runtime
`package.path`. The engine provides the real implementations. Shared annotations
in `_types.lua` and generated `_components.lua` are metadata only; do not require
them in gameplay. An IDE may index the whole annotation library for type checking,
but service values stay local to the scripts that import them.

## Migrating existing scripts

Add a local require for each engine service used by each file. For example,
existing `Hud.set_text(...)` calls need `local Hud = require("demi.hud")` at the
top. Do not assign that table to `_G.Hud` or rely on another file's imports.
Update console commands and generated Lua fixtures in the same way.

Replace the old single-file stub library in your editor configuration. Remove
old `demi.lua` annotations from LuaLS library paths so they do not falsely suggest
that global APIs remain available. Regenerate stubs into the new directory layout.
The old globals fail at runtime; there is no compatibility toggle.

Earlier flat paths such as `demi.network_session`, `demi.tls_client`,
`demi.audio_source`, and `demi.rigidbody3d` have been replaced by the domain paths
above, without aliases. Regenerate into a fresh annotation directory or move
obsolete generated stubs out of the existing library first. Stub export rejects
obsolete native annotation files rather than leaving misleading old imports
visible to LuaLS or deleting potentially edited files.

Repository examples, bundled helpers, package sources and Lua test fixtures use
explicit imports. Previously published package archives are not republished by
this source change; old installed copies may need updating independently.
