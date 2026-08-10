# Lightweight 3D Gameplay

DemiEngine uses Jolt Physics 5.6.0 behind `PhysicsWorld3D`. Jolt types do not
cross the engine boundary, so scenes and Lua code use stable DemiEngine
components and services on Linux and Android. The dependency is pinned in
CMake, cross-platform deterministic mode is enabled, and unrelated Jolt
samples, viewers, shader backends, and tests are disabled.

## Bodies and colliders

`Rigidbody3D` supports static, kinematic, and dynamic motion, gravity, mass,
linear/angular velocity and damping, friction, restitution, forces, impulses,
torque, continuous collision, sleep, runtime enable, translation/rotation
locks, kinematic targets, and render interpolation. Use `Rigidbody3D` from Lua
to issue runtime commands. Do not move dynamic bodies by repeatedly writing
`Transform3D`; physics owns their simulated transform.

One entity may have one explicit collider:

- `BoxCollider3D`
- `SphereCollider3D`
- `CapsuleCollider3D`
- `ConvexCollider3D`
- `ModelCollider3D`

Triangle-mesh `ModelCollider3D` is static-only. Moving objects use a primitive,
capsule, or convex hull. Validation rejects moving meshes, multiple colliders,
invalid capsule proportions, underspecified convex hulls, parented moving
bodies, and moving bodies without a collider. Complex compound objects use
child entities, each with one collider.

Collider import detail controls generated mesh data only. Runtime collision is
always determined by the collider component and its authored asset; the solver
does not substitute a visual-model bounding box.

Project collision layers and masks apply to 3D body pairs. Contacts produce
`physics3d_collision_enter`, `_stay`, and `_exit`, or matching
`physics3d_trigger_*` events. `physics3d_contact` receives both. Payloads
contain entity IDs, layers, phase, point, normal, penetration, and trigger
state.

## Queries

`Physics3D` provides raycasts, rich sphere/box overlaps, sphere casts, and the
C++ layer also exposes capsule overlaps and casts. Rich hits contain
`entity_id`, `layer`, `point`, `normal`, `distance`, `fraction`, and
`is_trigger`. Once the fixed-step world exists, queries use Jolt's narrow phase
against the same shapes as simulation.

```lua
local hit = Physics3D.sphere_cast(
  x, y, z, 0.12, direction_x, direction_y, direction_z, 30.0,
  "world", projectile_id)
```

## Character controller

`CharacterController3D` provides slope limit, step height, skin width, gravity,
grounding, wall slide, and moving-platform ground velocity. Collision geometry
is deliberately separate: add exactly one `BoxCollider3D`,
`SphereCollider3D`, `CapsuleCollider3D`, or `ConvexCollider3D` to the same root
entity. That collider owns the controller's dimensions, offset, transform
scale, and collision layer. A controller collider cannot be a trigger or a
triangle mesh, and a controller entity cannot also have `Rigidbody3D`.

For example, a cube character uses matching data rather than an implicit
capsule:

```json
"BoxCollider3D": {
  "size": [1.0, 1.0, 1.0],
  "layer": "player"
},
"CharacterController3D": {
  "step_height": 0.35,
  "slope_limit": 50.0,
  "skin_width": 0.02,
  "gravity": -20.0
}
```

The controller intentionally does not define coyote time or jump buffering:
those are game-feel policies and belong in gameplay code. Capture the pressed
edge in `on_update`, retain it for the desired buffer duration, and issue the
jump from `on_fixed_update` while the example's grounded grace period is
active:

```lua
function Player:on_create()
  self.jump_buffer_remaining = 0
  self.coyote_remaining = 0
end

function Player:on_update(dt)
  if Input.action_pressed("jump") then
    self.jump_buffer_remaining = 0.12
  end
end

function Player:on_fixed_update(dt)
  CharacterController3D.set_velocity(self.entity_id, vx, 0, vz)
  local state = CharacterController3D.state(self.entity_id)
  if state and state.grounded then
    self.coyote_remaining = 0.12
  else
    self.coyote_remaining = math.max(0, self.coyote_remaining - dt)
  end
  self.jump_buffer_remaining = math.max(0, self.jump_buffer_remaining - dt)
  if self.jump_buffer_remaining > 0 and self.coyote_remaining > 0 then
    CharacterController3D.jump(self.entity_id, 7)
    self.jump_buffer_remaining = 0
    self.coyote_remaining = 0
  end
end
```

The controller participates in trigger enter/stay/exit events even though its
selected virtual shape is not a dynamic rigidbody.

## Transforms and cameras

`Transform3D.forward`, `right`, and `up` return world directions, including
parent rotation. `Transform3D.look_at` rotates the local +Z forward axis toward
a world point. `Camera3D.screen_ray`, `world_to_screen`, and
`screen_to_world` accept explicit viewport dimensions, which keeps the math
deterministic and usable in headless tests.

The `minimal_3d` example is the reference probe. It uses public APIs for
capsule movement and grounding, jumping, trigger pickups, a kinematic moving
platform, and continuous rigidbody projectiles.

## Determinism and testing

Physics advances only on the project fixed timestep. Replay comparisons should
use tolerances rather than bitwise transform equality across architectures.
The focused 3D suite covers all six collision approach directions, thin-wall
continuous collision, trigger phases, layer masks, mass and impulses,
grounding and jump, exact narrow-phase shape queries, interpolation bounds,
camera conversion, and repeated world construction/destruction. Run memory
sanitizers in CI for lifetime regressions.
