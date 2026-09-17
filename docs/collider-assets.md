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

Prepared fracture prefabs may embed `ModelCollider3D.inline_geometry` instead of
an `asset` reference. It uses this same compound format/parser, including
`format_version`. Do not supply both sources. The normal workflow remains asset
references; [fracture authoring](fracture-authoring.md) generates embedded payloads
automatically and bakes them during cooking.

The existing `ModelCollider3D` component is the asset attachment; the Inspector
labels it **Collider Asset 3D**. It accepts convex and compound collider assets on a dynamic
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

## Compound assemblies

`shape: "compound"` defines 1–256 convex parts in one asset. Each part has a
unique local `id` and 4–256 non-coplanar `points`, using the same hull validation
as standalone convex sources. Points are expressed in the shared asset origin;
there are no nested compounds or external child references in this first version.
IDs start with an ASCII letter/digit and may contain letters, digits, `_`, `-`,
and `.`, up to 128 characters. Do not use array positions as durable identity.

```json
{
  "format_version": 1,
  "shape": "compound",
  "parts": [
    { "id": "left", "points": [[-2,0,0], [-1,0,0], [-2,1,0], [-2,0,1]] },
    { "id": "right", "points": [[1,0,0], [2,0,0], [1,1,0], [1,0,1]] }
  ]
}
```

Attach it with the same `ModelCollider3D` component. The compound is **one native
body**, with real gaps between its hulls. `Rigidbody3D.mass` is the total mass;
Jolt derives compound center of mass and inertia from its uniformly dense hulls,
scaled to that total. The immutable compound shape can belong to a static,
kinematic or dynamic body. Mirrored/nonuniform entity scale is applied to each
part's points. Visual child entities may follow the body via `Transform3D.parent`;
do not give those children extra colliders unless independent bodies are intended.

After physics synchronization, `Physics3D.raycast` includes optional
`collider_part_id` when a compound part is hit. Native subshape indices remain
private. The body keeps its own part-ID snapshot, so a loaded/reordered asset
does not mislabel hits while the old native shape is still in use. Shape casts
and overlap queries use the real compound but do not yet report part IDs.
Before the first physics synchronization, legacy cold-world query fallbacks
omit compounds rather than treating their union bounds as solid collision.
Editor selection uses the union bounds; collider debug drawing shows the hulls.

The asset follows the normal import, cook, reload and live-user retention rules.
An optional `fracture` block adds an authored bond graph; see below. Per-chunk
materials/density and transactional split-body replacement remain separate
Milestone 3 work. See `examples/destruction_3d_lab` for an editable three-part
arch and native impulse/raycast probe.

Triangle-mesh collider assets remain static-only. Asset-backed character-controller
movement shapes are not enabled by this change; characters still use their existing
inline box/sphere/capsule/convex components. This format does not yet define sphere,
capsule, or 2D collider assets.

## Optional fracture graph

Compound sources can add a graph over their existing part IDs, without copying
geometry or creating another asset type. It is covered by root `format_version: 1`.

```json
"fracture": {
  "anchors": ["left", "right"],
  "bonds": [
    { "id": "left-lintel", "parts": ["left", "lintel"] },
    { "id": "right-lintel", "parts": ["right", "lintel"], "health": 5 }
  ]
}
```

This example assumes parts named `left`, `right` and `lintel`. Bond IDs use the
same local identifier rules as parts. Each pair references two distinct existing
parts; repeated pairs (including reversed pairs) and duplicate IDs are invalid.
There are at most 2,048 bonds, and they must connect every part into one initial
assembly. A single-part compound may have an empty bond list. Optional `anchors`
defaults to `[]` and contains unique existing part IDs. Optional bond `health`
defaults to `1` and must be finite and positive. Unknown graph/bond fields are
rejected to catch authoring typos. A standalone `convex_hull` cannot have this
block; use a one-part compound if needed.

These are authored connections, not automatically detected contacts. Authors
must place the hulls/visuals appropriately. Health is a damage threshold, not a
material-strength calculation. Anchors identify which connected groups remain
supported; they do not change the current rigidbody's motion by themselves.

Import, reimport, direct/registry validation and cooking use the same parser.
Invalid graph edits reject reimport before updating the manifest hash. Valid
graph edits update the normal source hash/revision. Cooking retains the graph
inside the collider source, with no external graph dependencies, native Blast
blobs or generated cache mirrors. `demi asset inspect ... --format json` reports
the normalized bonds and anchors. The editor's existing Collider3D import and
asset picker paths still apply; there is no dedicated bond-graph editor yet.

The runtime loader preserves graph metadata and accounts for it in decoded and
resident payload estimates. `createColliderFractureFamily3D` converts a loaded
snapshot into independent Blast state, deriving local-space centers and volumes
from Jolt convex hulls rather than bounding boxes or hand-entered values. Hulls
that Jolt cannot construct fail preparation with an error. Existing families
own their snapshots; asset reload/unload does not reset their accumulated damage.

**Adding this block alone does not enable destruction.** Opt in with
`Destructible3D` and map parts to their visual children. The arch demonstrates
queued part damage and native physical/visual splits. Spatial blast falloff,
general concave fracture authoring and the time-budgeted scheduler remain pending.
For normal convex-mesh authoring, use `Destructible3D` and `Fracture3D` components
in one prefab; [the shared compiler](fracture-authoring.md) generates this data.
See [destruction runtime status](3d-destruction-runtime.md).
