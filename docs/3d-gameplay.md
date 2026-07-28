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

`CharacterController3D` is a reusable virtual capsule with slope limit, step
height, skin width, gravity, grounding, wall slide, and moving-platform ground
velocity. Set horizontal velocity in `on_fixed_update`, request a jump only
when desired, and query state for grounding:

```lua
function Player:on_fixed_update(dt)
  CharacterController3D.set_velocity(self.entity_id, vx, 0, vz)
  if Input.action_pressed("jump") then
    CharacterController3D.jump(self.entity_id, 7)
  end
  local state = CharacterController3D.state(self.entity_id)
end
```

The controller participates in trigger enter/stay/exit events even though its
virtual capsule is not a dynamic rigidbody.

## Transforms and cameras

`Transform3D.forward`, `right`, and `up` return world directions, including
parent rotation. `Transform3D.look_at` rotates the local +Z forward axis toward
a world point. `Camera3D.screen_ray`, `world_to_screen`, and
`screen_to_world` accept explicit viewport dimensions, which keeps the math
deterministic and usable in headless tests.

The `minimal_3d` example is the reference probe. It uses public APIs for
capsule movement and grounding, jumping, trigger pickups, a kinematic moving
platform, and projectile-style sphere casts.

## Determinism and testing

Physics advances only on the project fixed timestep. Replay comparisons should
use tolerances rather than bitwise transform equality across architectures.
The focused 3D suite covers all six collision approach directions, thin-wall
continuous collision, trigger phases, layer masks, mass and impulses,
grounding and jump, exact narrow-phase shape queries, interpolation bounds,
camera conversion, and repeated world construction/destruction. Run memory
sanitizers in CI for lifetime regressions.
