# Water Gameplay

Opt-in fixed-step buoyancy, flow-relative drag and dynamic-body swimming over
prepared terrain water. No renderer, automatic listeners or fluid simulation.
Import the specific service you need:

```lua
local Buoyancy = require("demi.gameplay.water.buoyancy")
local Swimming = require("demi.gameplay.water.swimming")
local float = Buoyancy.new("boat", {half_height = .5, density_ratio = 1.05})
local swimmer = Swimming.new("player", {speed = 3, min_depth = 1.5, max_flow = 2.5})

function Behaviour:on_fixed_update(dt)
  local floating = float:fixed_update(dt)
  local swimming = swimmer:fixed_update(dt, {1, 0, 0}) -- world-space direction
end
```

A game owns input/camera conversion, character animation, oxygen, transitions
between land and swimming controllers, and choosing which entities participate.
These modules do not change gravity, position, collider shape or body type.
Use Rigidbody3D dynamic bodies with authored colliders. Static, kinematic,
disabled and missing bodies receive no forces. A missing/dry water column also
receives no force. Native contacts remain authoritative.

## Buoyancy

`Buoyancy.new(entity_id, options)` accepts `half_height` (world units, default
0.5), `density_ratio` (default 1.05), `linear_drag` (per second, default 2),
`gravity` (world acceleration vector, default `{0,-9.81,0}`), and optional
`terrain_id`. Density ratio scales displacement lift relative to configured body
mass; larger values make the virtual body float higher. It is a gameplay ratio,
not a measurement of the collider's true displaced volume.

The virtual volume is a constant cross-section upright segment centred on the
body origin. Its overlap with surface-to-bed water is measured along native
`column_up`, including transformed terrain placements. It does not sample hull
vertices, apply torque or stabilize rotation. Gravity acceleration is explicit:
match it to the game's physics gravity and body gravity scale when seeking
neutral equilibrium. Lift still acts if native body gravity is disabled; the
package never toggles or silently restores that property.

Drag damps velocity relative to authored water flow using an exponential step;
its drag-only response is independent of timestep subdivision and cannot reverse
relative velocity. Buoyancy plus native contact/gravity remains a fixed-step
approximation. Both lift and drag use `Rigidbody3D.add_force`, not teleportation.

## Swimming

`Swimming.new(entity_id, options)` accepts `speed` (world units/second, default
3), `acceleration` (world units/second squared, default 8), `linear_drag` (default
2), `min_depth` (default 1.5 world units), `max_flow` (default 2.5 world
units/second), and optional `terrain_id`. These are game policy, not native
water eligibility rules. The body origin must be immersed; shallow water or a
flow above the chosen limit leaves land movement to the caller.

`fixed_update(dt, world_direction)` clamps direction length to one, accelerates
toward the chosen velocity relative to flow, then applies water drag. Drag can
reduce the attained speed. Vertical input permits ascent/descent. Gravity is
unchanged: add Buoyancy if desired, or author the swimming body's gravity policy.
Do not simultaneously apply a separate land controller's movement force.

Both update methods return a retained state table (`active`, `sample`, and for
buoyancy `submersion`). Read it immediately; the next update replaces its values.
Copy values that a game needs to retain. Use `demi.terrain.water.tracker` directly
for point enter/stay/exit events; these modules do not invent collider events.

All options, fixed timesteps and input directions must be finite. Fixed timestep
and half-height must be positive; rates and thresholds are nonnegative. Call
exactly once per physical step, in `on_fixed_update`; calling twice adds forces
twice. Shader water ripples change shading normals only, so gameplay samples and
floating objects use the same underlying flat/follow-bed surface.

`examples/water_interaction_3d` demonstrates real native contacts, buoyancy and
swimming with deterministic E2E assertions. Package tests exercise math and
configuration; `demi-water-gameplay-tests` exercises these same Lua modules
through the actual Lua/physics runtime without a window.
