# Reusable 3D collider files

Author a self-contained `*.collider.json` source and import it as a `Collider3D`
asset. The collider does not require a model or prefab. Its manifest supplies the
stable `asset://` identity; the source contains the geometry.

```json
{
  "format_version": 1,
  "shape": "convex_hull",
  "points": [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]]
}
```

The current authored format supports convex hulls with 4–256 finite local-space
points and nonzero volume. Points need not be ordered. Units are meters, Y-up;
put any desired offset in the points themselves. Bounds are derived rather than
duplicated in the file. Degenerate/coplanar sets are rejected. The schema is
`schemas/collider.schema.json`, with geometric validation in the shared loader.
Existing model-generated box/triangle-mesh collider assets remain supported.

```sh
demi asset import props/barrel.collider.json --project path/to/game --id asset://colliders/barrel
demi asset inspect path/to/game/assets/colliders/barrel/barrel.collider.asset.json --format json
demi validate path/to/game/assets/colliders/barrel/barrel.collider.json
```

The suffix selects the `collider-shape` importer automatically. Ordinary `.json`
files still default to data assets. Reimport after editing the collider source:

```sh
demi asset reimport path/to/game/assets/colliders/barrel/barrel.collider.asset.json
```

## Attaching a collider

The existing `ModelCollider3D` component is the asset attachment; the Inspector
labels it **Collider Asset 3D**. It now accepts a convex collider asset on a dynamic
rigidbody. There is no need to create a prefab or repeat `ConvexCollider3D.points`.
Inline colliders and optional prefabs remain available. The Inspector's Asset
picker lists only `Collider3D` assets, not render models or other asset types.

Preload the model and collider through the project's normal `assets` list:

```json
"assets": ["asset://models/barrel", "asset://colliders/barrel"]
```

Then create any number of independent bodies from Lua:

```lua
Entity.create("barrel_1", { components = {
  Transform3D = { position = { 0, 3, 0 } },
  MeshRenderer = { model = "asset://models/barrel", color = { 1, 1, 1, 1 } },
  ModelCollider3D = { asset = "asset://colliders/barrel" },
  Rigidbody3D = { body_type = "dynamic" },
} })
```

The same component JSON works in authored scenes and prefabs. Collision layer and
trigger status remain per-instance `ModelCollider3D` fields; mass, velocity,
friction, and sleeping remain rigidbody properties. The file is geometry, not a
gameplay behavior or prefab. Each body remains independent; sharing the definition
does not imply sharing a native rigidbody.

## Loading, lifetime, and validation

- Explicit project preloads load before gameplay. Authored scene references use
  normal scene asset discovery; unreferenced assets do not become resident merely
  because they have a manifest.
- For later scripted creation, call `Assets.load("asset://colliders/barrel")`
  and wait for `Assets.is_ready(request)` before creating users. Loading is handled
  by the normal asset service in standalone and embedded/editor Play runtimes.
- Reimport/reload of a changed same-ID asset changes its physics revision so native
  hulls rebuild. Scene changes preserve resident shapes and persistent users.
- `Assets.unload` releases asset-service residency. Live enabled physics users
  retain their shape snapshot so collision does not disappear underneath them.
  Unused shapes are released immediately; formerly live snapshots are retired
  on a subsequent physics step once they have no enabled users.
  These retained physics snapshots are separate from asset-service memory reporting.
- Runtime raycasts and collision use the convex shape, not its bounding box.
  Debug collider drawing uses its hull; editor selection bounds use derived bounds.
- The same parser is used for direct source validation, registry validation,
  import/cook, inspection, and runtime loading. Cooking keeps the collider source
  inside the audited cooked asset tree with stable references.

Triangle-mesh collider assets remain static-only. Asset-backed character-controller
movement shapes are not enabled by this change; characters still use their existing
inline box/sphere/capsule/convex components. This format does not yet define sphere,
capsule, compound, or 2D collider assets.
