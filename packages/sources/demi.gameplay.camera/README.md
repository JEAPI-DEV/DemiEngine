# 2D camera follow

Package: `demi.gameplay.camera`.

Computes a 2D follow position with velocity look-ahead, rectangular zones, and
optional shake. Exports `demi.gameplay.camera`, with no package dependencies
or event bus. See [package installation](../../README.md).

```lua
local Camera = require("demi.gameplay.camera")

local camera = Camera.new({ x = 0, y = 0, smoothing = 10, look_ahead = 0.2 })
camera:set_zone("room", { left = 0, right = 20, top = 0, bottom = 10 })
local pose = camera:update({ x = 5, y = 3 }, { x = 2, y = 0 }, 1 / 60)
assert(pose.zone == "room")
-- Apply pose.x and pose.y to the game's camera transform.
```

Call `update(target, velocity, dt, random)` each step with a target position,
a velocity table (which may be empty), and elapsed seconds. Smoothing uses
`min(1, smoothing * dt)` as the interpolation factor; zero smoothing holds
the current position rather than snapping to the target.

Only zones containing the target participate. Highest priority wins, followed
by lexicographically smallest ID. The selected zone clamps the desired
position before smoothing; it does not strictly confine the final camera
position or viewport edges. `set_zone` retains and adds `id` to the supplied
table. `remove_zone(id)` removes it.

`shake(amplitude, duration)` adds an offset while its duration remains positive.
Supply `random`, a function returning values in [0, 1], to `update` for visible
shake. Without it, offsets are zero. Reproducible shake requires a reproducible
random sequence and update steps; the package supplies no random generator.
Shake is added after smoothing and can extend outside zone bounds.

This module does not create or move engine cameras, subscribe to events,
detect obstacles, or calculate 3D camera rotation. The game applies the returned
pose and owns the camera helper's lifetime.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.camera
```
