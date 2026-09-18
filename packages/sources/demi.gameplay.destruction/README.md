# Destruction weapon probes

Requires the current DemiEngine development build with the experimental native
destruction APIs. Install from a project directory with
`demi package add demi.gameplay.destruction@1.1.0 --registry /path/to/DemiEngine/packages`.
Version 1.1.0 is currently a local development release; the hosted 1.0.0 archive
is unchanged. See `LICENSE` for BSD-3-Clause
terms. This is gameplay code; no third-party models or textures are included.

`demi.gameplay.destruction.weapons` owns hammer phases, ammunition, cooldowns,
bounded rocket flight and cleanup. `demi.gameplay.destruction.native` adapts public
engine queries, prefab visuals and native spatial impacts. No shard names or
physics-body splitting rules appear in the package.

```lua
local Weapons = require("demi.gameplay.destruction.weapons")
local Native = require("demi.gameplay.destruction.native")
local weapon = Weapons.new(Native.new({owner = "player", rocket_prefab = "prefab://rocket"}),
  {id_prefix = "player/rocket/"})
```

Call `hammer()` to begin an attack, `fire(origin, direction)` for a rocket, and
`update(fixed_dt, origin, direction)` every fixed step. The hammer queries at the
end of its swing, not at button press. Rockets sweep a sphere over their entire
step, expire without exploding, and detonate once on first contact. Initial
overlaps detonate without spawning a visual. `dispose()` releases active visuals.
`reload()` is an immediate diagnostic refill, only allowed with no active rockets.

The visual prefab must have an unparented Transform3D root named `body` (override
with `rocket_root`) and no collider/rigidbody: propelled rockets use native swept
queries, not independent ballistic bodies. A unique `id_prefix` is required for
each weapon owner. Supply all services explicitly for isolated unit testing;
native modules are imported only by the native adapter.
The visual root follows the swept-sphere centre and faces local +Z. Keep the
model's leading tip within `rocket_radius`; the example offsets its shell so
the visible nose and collision front agree.

`hammer_phase()` returns phase/progress for visual animation. `drain_events()`
returns bounded presentation events (the latest 32), not authoritative damage.
Counters `shots`, `detonations`, `contacts`, `ammo`, and `rockets` can be polled.
An accepted impact is queued; native destruction may still reject its commit.
The native adapter attaches an optional `receipt` to impact events, containing
the directly hit target's stable assembly `root`, pre-queue `revision`, and
original `target` body ID. Poll `Destruction.state(receipt.root)` to distinguish
queued, failed and revision-advanced batches; the original body may be retired
by a split. This tracks the primary target's assembly, not all assemblies touched
by a radial explosion, and is diagnostic rather than a per-request transaction ID.
Custom impact services can return this metadata as their fourth result; the
optional second service argument is the directly hit body (not a radial filter).

This is a local gameplay foundation, not network authority, an inventory system,
ballistic rocket physics or a full animation system. Initial overlap checks and
native sweeps ignore trigger volumes while retaining blocking geometry behind them.

## Streamed structures

`demi.gameplay.destruction.streaming` accepts compact `{id,prefab,position}`
records and `Native.streaming()` services. Records allocate no engine entities
until they enter range. `update(dt, observer_position)` uses a spatial index,
bounded scan/spawn budgets and an active-instance cap. Defaults: 24 m load,
32 m unload, 8 active, 1 spawn/update, 256 candidates/update. Distance uses
assembly origins, not individual debris bounds. First-use generation is synchronous.

Damage is retained in memory on unload (`preserve=false` explicitly resets a
record instead). `debris_lifetime=0` disables cleanup; a positive value opts into
retiring detached groups after idle active time, while preserving the resulting
holes. `export_state()` returns a versioned, geometry-free save table; pass it to
`demi.save` only when disk persistence is wanted. `import_state(state)` must run
before activation. Template mismatch fails rather than silently restoring a
pristine wall. Check `last_error`; `active_count` and `last_scanned` are diagnostics.

`dispose()` releases this manager's instances; export first if their state must
survive disposal. This package is policy over native checkpoint/restore/cleanup,
not an alternate Lua physics or fracture implementation. See the engine's
`docs/streamed-destruction.md` for authoring, cache limits and qualification gaps.
