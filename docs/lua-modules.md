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
| Networking | `demi.network`, `demi.network.session`, `demi.network.http`, `demi.network.tls.client`, `demi.network.tls.server`, `demi.network.crypto` |
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

### Spatial values and failure results

Individual spatial queries may return scalar tuples, such as
`local x, y, z = Transform3D.get_position(id)`. Embedded and bulk vector
values use fixed-length, one-based numeric arrays: `{x, y}` (`Vec2`) or
`{x, y, z}` (`Vec3`). This is intentional idiomatic Lua. The shared aliases
describe tables, not multiple return values or objects with `.x` fields.
Read each signature: even individual `Entity.local_position`/`world_position`
queries return arrays, while transform getters return tuples.
Some event records expose individual coordinate fields such as `point_x`;
these are not embedded vector objects. Keep those service-specific shapes.

```lua
local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Vector3 = require("demi.math.vector3")
local x, y, z = Transform3D.get_position("player") -- local-space tuple
if x ~= nil then
  local offset = Vector3.add({x, y, z}, {0, 1, 0}) -- numeric array
  Transform3D.set_position("player", offset[1], offset[2], offset[3])
end
local world = Entity.world_position("player") -- array or nil
```

Missing transform/body vector getters return one nil per coordinate (two or
three return values). Missing state records and missed raycasts return a single
nil; bulk physics queries return empty tables. `Grid.path` returns
`nil, diagnostic` on failure, whereas `Navigation2D.path` returns an empty
table and a diagnostic. Navigation path entries contain numeric cell indices
`[1]`/`[2]` plus named `world_x`/`world_y` cell centers, so they are richer
records than plain vectors. Error records are also service-specific:
`Data.load` returns `nil, {code=..., message=..., path=...}` on failure and
`value, nil` on success. JSON null in the data service is `Data.null`, not nil.

Transform position/rotation/scale getters read local components. Transform2D
rotation and Transform3D Euler angles are radians; rigidbody angular velocity
is radians per second. Camera FOV is degrees. Camera conversions and query-hit
vectors have their own spaces; see [3D gameplay](3d-gameplay.md#transforms-and-cameras).

### Entity configuration versus simulation state

Exact-ID lookups, local Transform and Sprite configuration, and replication
capture can access entities queued by `Entity.create` or `Prefab.instantiate`
in the same callback, before the command buffer commits. A queued replacement
takes precedence over the committed entity for these direct accesses. Name
lookups, physics spatial queries, and camera/hierarchy queries still use committed world
state; pending hierarchies are not projected into simulation queries.
`Rigidbody3D.state` reads the pending body component when present, so its initial
state is available before simulation begins.

`Entity.get_config(id, component, field)` reads the last configured value. An
omitted value may be nil; this is not a snapshot of live physics or animation.
Use the relevant service, such as `Rigidbody3D.state`, for simulation state.

`Entity.set_field(id, component, field, value)` validates and updates one field,
preserving unrelated live fields and subsystem handles. It updates the in-memory
configuration but does not write the authored scene. The former `Entity.get`
and `Entity.set` names have been removed.

`Entity.spawn(id, options)` expands position/velocity shorthand and returns
`true, nil` when creation is queued, or `false, diagnostic` on rejection.
Shorthand vectors must be dense numeric arrays of exactly two or three finite
coordinates representable as floats; position and velocity must have matching
dimensions when both are provided. Explicit component fields take precedence
over valid shorthand. Invalid component blocks are rejected.

Supplying `prefab` or `ttl` is rejected with guidance to use `Prefab.instantiate`
or `Timer.after`. There is no native `Entity.spawn_ttl` API. The Lua
`Script.spawn` helper still handles its own optional TTL through a script-owned
timer and forwards only entity-creation options to the native service.

Component owners declare typed runtime field bindings. New reflected fields must
have a binding; compilation fails if one is missing. Compound authored properties
can update several members explicitly, such as the two sides of a nine-slice.

### Events have different owners

Native `demi.events` dispatches immediately to host-owned subscriptions and script
event handlers. The optional `demi.gameplay.events` package creates caller-owned
queues dispatched by `flush()`. Choose immediate engine/script notification or
queued gameplay processing deliberately; these are not interchangeable buses.

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

## Native implementation ownership

`LuaServiceModules.cpp` declares the native service/import catalog explicitly.
Import names are not derived from C++ identifiers or discovered from arbitrary
uppercase globals. A service absent from a host is not published. Unknown service
lookups fail instead of inventing an import path.

`LuaScriptHostBindings.cpp` installs binding modules. Entity operations live in
`bindings/LuaEntityBindings.cpp`; prefab creation, pooling and template settings
live in `bindings/scene/LuaPrefabBindings.cpp`. Mesh builders and voxel section
construction live in `bindings/mesh/LuaMeshConstructionBindings.cpp`. The latter
converts Lua inputs into typed blocks, tile mappings and bordered heightfields.
`runtime/geometry/VoxelMeshBuilder.h` owns mesh construction and section neighbor
queries without Lua dependencies. Native tests exercise that same implementation.

When adding a service, register its import in the catalog, add the matching stub,
and update the contract tests. Keep return types in the stub of their owning
service (for example, network responses belong to `demi.network`, not the grid).
Userdata metatables are implementation details; obtain instances through the
public module's factory, not a global type table.
