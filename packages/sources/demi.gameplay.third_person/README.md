# Third-person foundation

Three independent modules over ordinary Lua values:

- `demi.gameplay.third_person`: camera-relative movement, stamina and directional rolls.
- `demi.gameplay.orbit_camera`: mouse/lock-on orbit and obstruction distance.
- `demi.gameplay.melee`: windup, active, recovery and one hit per target per swing.

Construct with `Character.new(options)`, `Orbit.new(options)`, and
`Melee.new({windup=0.8, active=0.15, recovery=1.2, reach=2.5, damage=20})`.
See `examples/third_person_foundation/scripts/room.lua` for the complete adapter.
Call attack update and character input for all participants before resolving hits
in the fixed tick. Check `character:invulnerable()` at hit resolution.
These modules never translate entity transforms themselves: pass velocities to
`CharacterController3D.set_velocity` so static collision remains authoritative.

The camera cast callback returns nearest blocking distance. A missing callback
disables obstruction. Capture/release the mouse in the owning script lifecycle.
The room deliberately uses procedural pose cues; production animation clips and
bone-attached hit volumes still require integration. Reach/cone tests are an
approximation, not mesh collision. GPU skinning and retargeting are separate work.

Run `demi package test packages/sources/demi.gameplay.third_person`.
