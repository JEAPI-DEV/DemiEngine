# Lightweight 3D Workflow

The engine normalizes imported models to meters, Y-up, and +Z-forward before
rendering, animation, bounds inspection, or collider generation. Games should
not rotate or rescale a model to compensate for its source coordinate system.

## Import and inspect a model

```sh
demi asset import character.glb --project demi.project.json \
  --id asset://models/character --preset animated_character \
  --up +y --forward +z --meters-per-unit 1
demi asset inspect assets/models/character/character.asset.json \
  --section nodes,meshes,materials,textures,skeletons,animations,bounds
demi asset inspect assets/models/character/character.asset.json --format json
```

The presets are `static_prop`, `animated_character`, `environment`, and
`billboard`. Explicit axes and units remain stored in `settings.model_import`
inside the asset manifest. Inspection is read-only and works in headless CI.
It reports source names, normalized bounds, geometry cost, missing vertex
attributes, mirrored/non-uniform transforms, skeleton issues, clip names, and
external dependencies.

## Choose a collider deliberately

```sh
demi asset collider character.asset.json --recommend --body character
demi asset collider wall.asset.json --recommend --body static \
  --id asset://colliders/wall --detail 1 --preview wall.collider.scene.json
```

Recommendations are explanations, not mutations. Characters use an explicitly
authored capsule/box/convex collider beside `CharacterController3D`; dynamic
bodies use convex geometry; triggers use a simple primitive; static bodies may
accept exact triangle geometry. Generated manifests retain the source asset,
hash, import profile, body intent, and detail, so validation rejects stale
colliders after the model changes.

## Prefab and presentation starting points

The `lightweight-3d` project template includes ordinary prefab data for:

- first- and third-person controllers with explicit capsule colliders;
- moving platforms, pickups, continuous projectiles, and interactive doors;
- a camera rig and a mobile daylight environment.

Their behavior lives in small Lua modules under `scripts/controllers`. Replace
those modules or map animation state from them without changing engine code.
The daylight preset is composed only from `Environment3D`,
`DirectionalLight`, and normal camera/post-process components.

## Debug views

Set `Camera3D.debug_mode` to one of `shaded`, `normals`, `uv`, `alpha`,
`lighting`, `bounds`, `colliders`, `overdraw`, or `instancing` in scene data.
Bounds and collider lines use the resolved world transforms and collider data
used by runtime systems. In the instancing view, green geometry shares an
instanced submission and red geometry does not. The overdraw view is an
additive approximation intended for comparisons, not an exact GPU fragment
counter.

## Mobile cost gates

Declare limits in `performance_budgets` and inspect them without opening a
window:

```sh
demi asset budget demi.project.json --platform android
demi asset budget demi.project.json --platform linux --format json
demi validate demi.project.json
```

The report covers visible instances, unique mesh/material groups, procedural
triangles, estimated decoded texture memory, lights, shadow lights, and
transparent draws. Android defaults are intentionally conservative:

| Cost | Default Android limit |
|---|---:|
| Visible instances | 1,000 |
| Unique meshes | 128 |
| Procedural triangles | 250,000 |
| Decoded texture memory | 128 MiB |
| Lights | 8 |
| Shadow lights | 1 |
| Transparent draws | 128 |

Use model inspection for per-model triangle totals. Runtime profiling remains
the authority for frame time and actual draw submissions.

## Reference probe

`examples/minimal_3d` is the maintained exploration probe: it imports a glTF
model, uses an explicit player collider, crosses a kinematic platform, fires a
continuous projectile, enters a trigger, and collects a pickup. The same
project is validated and smoke-tested headlessly; Android packaging uses the
same scene and assets. The `lightweight-3d` template is the smaller reusable
starting point for a new game.
