# Compact, streamed destruction

The weapon lab stores a wall recipe and placement records, not a separate brick
collection for every wall in the map. `Masonry3D` generates a rectangular region
under a `Destructible3D` owner. Openings are composed from a few regions and
ordinary props such as a lintel and door.

```json
{
  "id": "pier",
  "components": {
    "Transform3D": { "position": [-1.375, 1.2, 0] },
    "Masonry3D": {
      "size": [1.25, 2.4, 0.115],
      "columns": 4,
      "rows": 24,
      "texture": "asset://textures/bricks085",
      "height_map": "asset://textures/bricks085_height",
      "anchor_below": 0.001
    }
  }
}
```

The owner supplies the deterministic seed and body budget. Density defaults to
1800 kg/m³ and bond health to 0.5. Size is positive metres; rows/columns are
positive integers whose product fits a signed 32-bit index. There is no
256-cell or assembly-part cap. Memory and generation cost still grow with detail.
`anchor_below` uses assembly-local Y. An optional `models`
object can select developer-authored shared meshes instead of a height map;
the native height-map path does not need model assets.

Cooking expands the recipe into the build output; authored prefabs remain
compact. Running uncooked source expands it when the prefab is instantiated.
The prefab service caches up to 16 eligible compact templates by
source content hash and rebases stable entity/visual IDs per instance. Source
edits invalidate the key. Transform-only overrides have their own bounded cache
key, so repeated placements with the same rotation/scale share preparation.
Nested prefabs, other overrides, or source-model fracture
inputs use the uncached path. Repeated instantiation no longer expands twice.
The editor uses the same cell mesh, atlas UV and height-relief descriptors as
runtime. Preview cells have no fracture component and are not saved into source.
Picking one selects its authored masonry region; Inspector edits and Undo/Redo
rebuild the preview from the recipe. This accurate preview costs more than the
old stretched-box approximation. Runtime activation uses the compact regional
representation described below.

## Relief without generated assets

`Masonry3D.height_map` adds `SurfaceRelief3D` to derived visuals. `texture_grid`
defaults to `[8,14]`; cells cycle through image regions. `relief_depth` defaults
to 0.012 m. Color and height images are source assets. The renderer generates
closed, indexed meshes in memory and instances identical variants across walls.
There are no generated GLBs, model manifests, or vertex arrays in authored or
cooked wall definitions. Cooked definitions contain cell and fracture metadata;
relief surface meshes are still prepared by the renderer.

Standalone `SurfaceRelief3D` accompanies a cube `MeshRenderer`. It accepts
`height_map`, `depth`, `height_min`/`height_max` (default 0.45/0.9), `uv_offset`,
`uv_scale`, and integer `segments` (default `[24,10]`, each 1–64). An empty height
reference leaves relief inactive for incremental editor authoring. The image's
red channel is treated as height data, not sRGB color. Depth must remain within
45% of the cube thickness. Skinning, imported models, inline vertices and mesh
denting cannot be combined with this component. Fracture uses `pieces: 1` and
an explicit box proxy; surface chips do not add collision complexity or mass.

`tiles` and `atlas_grid` default to `[1,1]`. Cooked masonry uses these to draw an
intact region as one indexed surface with the same cell UVs and relief as its
leaves. Tiles cycle through the atlas from the bottom row. An empty height map
with multiple tiles produces flat boxes. Dimensions must fit integer indices;
the generated mesh must fit the renderer's 32-bit vertex indexing.

`Environment3D.relief_image_cache_mb` (default 64) and `relief_cache_meshes`
(default 256) configure soft retention budgets. Zero disables budget eviction.
Visible meshes can exceed the retention budget; older unused variants are
evicted. A single image can exceed its cache budget. Image loading follows the
decoder and renderer's supported sizes, without separate 16 MiB/4096 limits.
Asset reload/unload and renderer shutdown clear the cache. Shared variants may
remain warm while individual walls unload. This is runtime CPU mesh generation;
tessellation, ray tracing, and a complete PBR/shadow implementation are separate
work.

## Lazy masonry visuals

Built-in masonry starts with one live visual per region. Cook stores leaf
descriptions in the owner's `Destructible3D.deferred_visuals`, rather than as
live entities. No generated source files need to be managed by the developer.
The normal part mapping and complete collision support graph remain available
for raycasts and damage.

After Blast computes new body groups, regions whose bricks still share one body
remain compact and follow that body. A region expands into leaf entities only
when its bricks separate into different groups. Other regions and walls stay
compact. Visual activation commits with body replacement; a rejected split
does not leave partial visuals behind. Checkpoint restore and debris retirement
use the same path.

The weapon lab doorway contains 12 live entities and 210 deferred brick
descriptions, compared with 222 live entities in the eager layout. The visible
smoke test used two shared regional relief meshes. This reduces entity and
render-resource overhead; the full surface detail and support graph remain in
memory. It is not a large-map frame-rate qualification.

This is a two-level visual hierarchy over Blast's support partition. Arbitrary
custom `Masonry3D.models`, deeper spatial refinement and on-demand loading of
fracture metadata remain open. Custom masonry models retain eager visuals.
Component-authored multi-piece source meshes now have a similar intact-to-shard
transition, including deferred interior surfaces; see
[fracture authoring](fracture-authoring.md). Legacy recipe visuals remain eager.

## Proximity and lifetime policy

### Editable scene placements

Prefer ordinary entities with `Transform3D` and `PrefabPlacement3D` over placement
records hidden in `GameplayData`. Nest them under the entity whose script owns
streaming:

```json
{
  "id": "world_stream",
  "components": {
    "Transform3D": {},
    "LuaScript": {"module": "script://scripts/world_stream.lua"}
  },
  "children": [{
    "id": "wall_a",
    "components": {
      "Transform3D": {"position": [0, 0, 0]},
      "PrefabPlacement3D": {"prefab": "prefab://doorway"}
    }
  }]
}
```

`prefab` is a normal prefab reference picker. `root` defaults to `assembly` and
names the prefab's single Transform3D root; set it to `body` or another root ID
when appropriate. `preserve` defaults to true. The placement's world position,
rotation and positive scale replace the prefab root pose, while child transforms
remain prefab-local. A placement is not a rigid body: put physics/destruction
components in the prefab itself.

`Prefab.placements(ancestor)` returns enabled placements beneath that entity,
sorted by stable ID, including resolved world position/rotation/scale. Disabled
ancestors exclude their placements. Omit the ancestor to read the whole world.
Reading records does not instantiate anything; streaming remains explicit game
policy:

```lua
local Prefab = require("demi.prefab")
local Streaming = require("demi.gameplay.destruction.streaming")
local Native = require("demi.gameplay.destruction.native")
self.stream = Streaming.new(Native.streaming(), Prefab.placements(self.entity_id))
```

The weapon lab uses local destruction package 1.1.0 for placement orientation
and scale support. The published 1.0.0 package has not been overwritten.
Read placements at startup; changing markers during gameplay requires rebuilding
the stream catalogue. Changing a saved placement's pose or root rejects that
checkpoint rather than restoring debris into the wrong location.

Editor preview uses the same prefab resolver with fracture compilation disabled.
It shows coarse masonry regions and ordinary meshes, not per-brick physics
bodies. Clicking preview geometry selects its authored placement for gizmos;
**Open source prefab** edits the actual prefab after saving/undoing dirty scene
changes. Preview entities never enter saved scenes or runtime worlds. This is
not an editor rendering-performance qualification for a 100,000-placement map.

### Programmatic catalogues

Import `demi.gameplay.destruction.streaming` with `Native.streaming()` from
`demi.gameplay.destruction.native`. Supply placement records:

```lua
local definitions = {
  {id="wall_a", prefab="prefab://doorway", position={0,0,0}},
  {id="wall_b", prefab="prefab://doorway", position={80,0,0}},
}
```

Records do not create entities. A ground-plane spatial index visits nearby
cells with a bounded scan budget (default 256 records/update). Defaults are
24 m load distance, 32 m unload distance, eight active assemblies and one spawn
per update. Hysteresis avoids activation churn. When full, the budget can
replace a farther ready wall with a candidate
at least two metres closer. Pending damage blocks checkpoint/eviction. These are
proximity limits, not a guarantee that every wall inside the radius is active.
Distances use assembly origins, not individual debris bounds. Unloading a region
leaves no distant wall proxy behind.
`activation_timeout` defaults to five game-time seconds; failed/stalled
activations release their slot and retain any previous checkpoint for retry.

Shared assets must be resident before activation: declare the two image assets
in project startup preloads or load their asset group explicitly. The streamer
does not force every project asset into memory or infer asynchronous asset loads
from arbitrary Lua prefab requests.

Active regions use one compound body per connected group. Their visual cells
exist only while active; geometry is shared. This first implementation activates
cells by proximity, not exclusively on impact. First-time template preparation
and mesh generation are synchronous; count budgets are not a millisecond budget.
Worker preparation, merged intact visuals and large-world FPS qualification
remain future work. The 100,000-record package test verifies bounded catalogue
scanning/instantiation; it is not a 100,000-rendered-wall benchmark.

`debris_lifetime` defaults to zero (disabled). A positive value retires detached
dynamic groups after that many active, idle seconds since the last change.
The lab opts into 45 seconds. Supported geometry and pristine dynamic roots are
not retired. Cleanup includes large loose props, so developers must choose this
policy deliberately. Per-material/important-debris exemptions are not implemented.

Distance unload preserves damage in memory by default. `preserve=false` on a
record discards it and resets that wall on its next activation.
Neither policy writes project files. No queued impact is silently discarded to
permit unloading.

## Optional persistence

`stream:export_state()` returns `{format_version=1,walls=...}` or an error while
activation/queued damage is incomplete. Only changed walls have checkpoints.
Use `Save.write(slot, state, 1)` if disk persistence is wanted, and call
`stream:import_state(Save.read(slot))` before activation. A save contains damage,
group membership, transforms/velocities and retired-piece flags—not mesh geometry
or full expanded entities. The default in-memory policy performs no save-file IO.

The lab's World Stream script exposes `save_slot` in its Inspector. Leave it
empty to disable disk persistence. Set it, press SAVE, and the next scene load
reads that slot. R reloads the scene; with a save slot configured it loads the
saved damage instead of a pristine reset.

Native `demi.physics.destruction3d` provides:

- `checkpoint(root)`: captures committed damage and physical state; rejects
  outstanding work.
- `restore(root, checkpoint)`: queues restoration on a fresh attached assembly.
  It validates format, template geometry/mass/scale fingerprint, IDs, numeric
  limits and supported-group topology. Poll `state` for completion/errors.
- `retire_debris(root)`: queues physical/visual removal of detached groups at the
  existing fixed-step transaction boundary. Missing pieces remain in checkpoints
  and do not resurrect when the wall returns. Retired groups do not consume the
  live-body budget.

Checkpoint restoration rebuilds the Blast graph and Jolt bodies, retaining mass
distribution, poses and velocities. Solver contact caches and sleep state are
not saved. Unloaded physics is suspended, not simulated in the background.
Changed/incompatible templates fail explicitly; automatic save migration and
multiplayer authority/replication are not part of this feature.

## Recorded Linux probe

The authored/cooked doorway shrank from 167,117 to 4,813 bytes. A fresh Linux
cook retained the recipe byte-for-byte and contained only the two referenced
images plus manifests under assets, with no GLB/Blender geometry.

The same three-frame headless probe measured first template preparation at
79.5 ms before removing a repeated ancestor scan and 37.0 ms afterward. The
second instance used the shared template (about 8.4 ms instantiation); these are
single-run measurements, not a frame-budget qualification. First use can still
hitch and needs worker/time-budgeted preparation for production workloads.

Reproduce with `DEMI_HEADLESS=1 demi run --project examples/destruction_weapons_3d_lab
--max-frames 3 --profiler --profile-report <path.csv>`. Relevant scopes are
`Prefab.prepare_template`, `Prefab.instantiate`, `Prefab.template_cache_entries`,
`Renderer3D.relief_generate` and `Renderer3D.relief_cache_variants`.
A separate 1080p/120-frame graphics probe generated 62 shared variants once
(14.3 ms total CPU preparation, including image decoding). Background activity
and display changes prevent treating that run as an FPS qualification.
