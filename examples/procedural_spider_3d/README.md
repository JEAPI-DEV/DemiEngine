# Procedural Spider 3D

This example demonstrates terrain-aware procedural animation without a baked
walk clip. It creates seeded stepped terrain and rocks at runtime, then moves
an eight-legged spider across the ground and onto a vertical wall. Change the
`terrain_seed` script property to produce another deterministic layout.

Each foot is placed with `Physics3D.raycast`. Alternating leg groups retain
planted contacts while their partners swing forward. Foot clearance stays low
on flat ground and increases only enough to accommodate a changed contact
height. Every frame uses
`Animation.solve_two_bone_3d` to place the knee and clamp unreachable targets;
`Animation.set_bone_segment` applies the result to named joints in the skinned
Blender model. The authored source, imported GLB, and asset manifest live under
`assets/procedural_spider/spider/`.

```lua
local solved = Animation.solve_two_bone_3d({
  root = hip_position,
  target = foot_contact,
  pole = outward_knee_hint,
  upper_length = 0.72,
  lower_length = 0.78,
})
Animation.set_bone_segment("spider", "leg_1_l_upper", {
  start = hip_position,
  tail = solved.joint,
  pole = outward_knee_hint,
})
```

Run it with:

```sh
./build/linux-debug/demi run \
  --project examples/procedural_spider_3d/demi.project.json
```

The matching `Animation.solve_two_bone_2d` API uses two-component vectors and
supports the same planted-limb technique in 2D games. General vector operations
come from `Vector2`, `Vector3`, and `Mathf`; the example contains locomotion and
terrain policy rather than reimplementing vector arithmetic.
