# 2D projectiles and weapon timing

Package: `demi.gameplay.projectiles`.

Provides 2D segment-based shots, pooled projectile state, and a separate
cooldown/ammo helper. Depends on `demi.gameplay.events` and exports:

- `demi.gameplay.projectiles`: moving shots advanced by `update(dt)`.
- `demi.gameplay.hitscan`: immediate segment queries through `fire`.
- `demi.gameplay.weapon`: firing permission and ammo consumption.

See [package installation](../../README.md). This example injects a sample
query result; replace the callback with a physics-query adapter:

```lua
local Events = require("demi.gameplay.events")
local Projectiles = require("demi.gameplay.projectiles")
local Hitscan = require("demi.gameplay.hitscan")
local Weapon = require("demi.gameplay.weapon")

local events = Events.new()
local function query(x1, y1, x2, y2, mask)
  return { { entity = "enemy", fraction = 0.5 } }
end
local shots = Projectiles.new(events, query)
local weapon = Weapon.new({ cooldown = 0.2, ammo = 3 })
if weapon:fire() then
  shots:spawn({ owner = "player", x = 0, y = 0, vx = 10, damage = 2, life = 1 })
end
shots:update(0.1)
weapon:update(0.1)
Hitscan.fire(events, query, {
  owner = "player", from_x = 0, from_y = 0, to_x = 10, to_y = 0, damage = 2,
})
events:flush()
```

Queries receive `(x1, y1, x2, y2, mask)` and return an array of hits or `nil`.
Each hit needs a string `entity`; supply `fraction` for ordering, and optional
`trigger`, `point`, and `normal`. Adapt native hit fields such as `entity_id`
to this contract. Hits are sorted by fraction, then entity ID. Both shot types
skip the owner, the single `ignored` ID, triggers, and repeated hits on the same
entity. `pierce = 0` stops at the first accepted hit; use a nonnegative integer
for additional hits.

The caller owns the bus and flushes after shot updates. Shots queue
`damage_requested`; moving shots also queue `projectile_spawned` and
`projectile_released` with the owner ID. Damage is not applied automatically:
subscribe and route requests to game health/damage rules. Unsubscribe when the
listener owner is destroyed. Release resets a projectile table and returns it
to the pool, so a retained reference may later represent another shot.

`Weapon:fire` only consumes ammo and starts cooldown; it does not create a shot.
Omitted ammo means unlimited shots. `update(dt)` reduces cooldown. The stored
`magazine` and `reload` fields have no reload behavior implemented.

These modules do not render shots, create physics bodies, apply damage, or
provide general 3D trajectories. Coordinates and velocity are x/y only; a 3D
query adapter would need its own plane/depth convention. Collision coverage
depends on the injected query, and pooling covers Lua tables, not entities.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.projectiles
```
