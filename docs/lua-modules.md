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
`require("demi")` import-all facade. Requiring a module does not create a
global for other scripts.

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

Transform services are `demi.transform2d` and `demi.transform3d`. The complete
native module inventory is the files under
`scripts/stubs/demi/` recursively, excluding underscore-prefixed type-only files.

The native host supplies these modules through Lua's standard preload mechanism.
`require` caches their tables per Lua state. Native bindings are still registered
at host initialization; importing a module only makes the dependency explicit
and does not change startup timing or permissions. Ordinary Lua helpers such as
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

Former Lua import names that still resolve differently or no longer exist are
listed in [legacy APIs](legacy-apis.md).
