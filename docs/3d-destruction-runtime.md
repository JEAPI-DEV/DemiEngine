# Destruction runtime integration

Milestone 3 is in progress. The arch now demonstrates localized bond damage,
connected-group physical splits, retained anchored collision and moving visual
parts. It is not yet the complete wall/hammer/rocket/steel-door scene.

## Persistent Blast damage state

`runtime/destruction/BlastFamily3D` is an internal C++ module linked through the
shared graphical/headless runtime dependencies. It owns the Blast asset and
family without exposing SDK pointers or indices to other engine subsystems.
The [compound collider format](collider-assets.md#optional-fracture-graph) now
supports authored bonds and anchors. `ColliderFractureFamily3D` bridges a loaded
collider snapshot to this module and derives chunk volumes/centers through Jolt.
`DestructionWorld3D` owns opted-in assemblies in a runtime world. `Destructible3D`
maps part IDs to visual children; the `Destruction3D` Lua API queues hits and
reports committed ownership.

Inputs are a connected flat graph of 1–256 support chunks and at most 2,048
bonds. IDs are unique ASCII identifiers, at most 128 characters, using the same
letters/digits/underscore/dash/dot convention as compound part IDs. Chunks carry
finite centroids (each coordinate bounded to ±1,000,000), positive volume and an
anchor flag. Bonds reference two distinct chunks and have finite positive
initial health. Duplicate endpoint pairs, dangling references and disconnected
initial graphs are rejected. Input ordering does not change the output groups.

The initial scope damages bonds, not individual chunk interiors. Different bond
health values allow weaker connections to fail before stronger ones. Damage
amounts are explicit gameplay units, **not** an implemented kinetic-energy or
material-strength model. An anchored group contains at least one authored
anchored chunk; the world coordinator makes that group static. It does not run
structural stress analysis. There are no virtual-world bonds or runtime anchor
changes yet.

## Staging contract

1. `stage` accepts a bounded batch of unique bond IDs and finite positive damage.
   It snapshots every live actor through Blast's supported serialization API
   into a separate family, preserving previously committed partial damage.
2. Blast applies damage and splits that staged family. `stagedGroups(token)`
   exposes sorted stable chunk IDs plus anchor state. The live family remains
   unchanged, including its health values.
3. The physics coordinator prepares replacement shapes, bodies and
   visual ownership **before** accepting this proposal. Failure or cancellation
   calls `discard(token)`; successful physical preparation calls
   `commit(token)` at the fixed-step transaction boundary.

Only one proposal can exist per family. Stage can allocate or throw; failed
staging leaves committed state untouched. Commit/discard do not allocate and
reject stale tokens. Tokens are family-local, not globally unique job handles.
The owner must attach its own family identity/lifetime when queueing future
work. Every committed batch increments the state revision, including a partial
hit that changes health without changing topology. Destroying the owner releases
both committed and pending storage. No raw SDK snapshots are serialized into
project files, save files or network messages.

The module is single-thread owned and synchronous. Copying actors, SDK damage
and split calls are not resumable jobs. Count limits and one staged copy are
safety bounds, not time budgets or a measured memory ceiling. Allocation-failure
injection and sustained memory/performance qualification remain pending.

## Build and tests

The engine and optional feasibility probes share `cmake/DemiBlast.cmake`, which
retains the existing pinned, hash-verified CPU-only Blast source subset. It does
not build PhysX or CUDA. Standalone probe builds remain supported.

```sh
cmake --build --preset linux-release --target demi-blast-family3d-tests
ctest --test-dir build/linux-release -R '^demi-blast-family3d-tests$' --output-on-failure
```

Coverage includes partial-hit persistence, cancellation without health leakage,
single-proposal limits, stale/double commit, stronger bonds, anchors, repeated
splits after actor serialization, input-order determinism, invalid graph/damage
inputs, singleton families, 256-way split cancellation and repeated lifetimes.
These tests exercise the shared module, not a separately reimplemented fixture.

`demi-fracture-asset-tests` additionally checks optional graph parsing, stable
IDs/defaults, invalid reimport rejection, source-hash changes, cook/validation,
runtime load/unload/reload, independent family snapshots and tetrahedral hull
volume/centroid derivation. The shared Jolt lifetime permits family preparation
before a physics world exists or while one is alive. Release pipeline/physics
regressions and Debug pipeline/physics tests pass; this is not Android gameplay
or destruction performance qualification.

Next: complete visual fracture assets, spatial damage resolution, bounded/fair
scheduling and the wall/hammer/rocket probe. Milestone 3 is not complete.

## Scene attachment and gameplay

Keep geometry in the existing compound collider asset. Add this to the root
alongside `Transform3D`, `ModelCollider3D` and `Rigidbody3D`:

```json
"Destructible3D": {
  "parts": { "left": "arch_left", "right": "arch_right", "lintel": "arch_lintel" }
}
```

Every collider part needs exactly one distinct, non-persistent direct renderer child without
its own rigidbody/collider. The root has no MeshRenderer. Root parenting,
kinematic/trigger bodies and persistent assemblies are not supported yet.
Anchored assemblies start static; unanchored assemblies start dynamic.
`Rigidbody3D.mass` supplies total mass even when initially static. `max_bodies`
defaults to 64 (1–256), bounding the final body count per assembly.
The Inspector exposes the component and numeric limit; the parts map is displayed
read-only like other object fields and is authored in scene/prefab JSON.
Prefab expansion remaps visual entity references, not collider part IDs.

Attachment happens after the first physics synchronization. Graph metadata alone
does not opt a collider into destruction.

```lua
local Destruction3D = require("demi.physics.destruction3d")

local accepted, issue = Destruction3D.damage_part(hit.entity_id, hit.collider_part_id, 0.6)
local state = Destruction3D.state("arch")
-- state.status: unattached, ready, queued, applied, failed
-- state.error, state.revision, state.bodies
local beam_body = state.parts.lintel
```

Damage targets still-connected bonds incident to the selected part, using authored
health units. A direct hit also damages that part's foundation attachment, if any.
Foundation health starts at the strongest incident authored bond health, or 1
for a singleton without bonds. Neighboring hits do not damage that attachment.
Once depleted, it no longer pins the connected group; remaining live anchors
still support the group. Anchor health is staged and rolled back with bond damage
and native body replacement. An isolated anchored shard can therefore be released
after all its neighboring bonds have already broken. Released leaf shards remain
whole: this API does not recursively subdivide them.
This low-level API is part-local damage. For radius selection, energy allocation
and post-commit impulses use [the spatial impact API](3d-spatial-impacts.md).
Structural stress and automatic collision-energy coupling are not implemented. Calls coalesce
by bond until the next fixed step. Acceptance means queued, not committed; check
the revision/status/error. Invalid IDs, non-positive/non-finite damage and overflow
reject without changing the queue. The root remains the assembly handle; query
`state.parts` for current body IDs instead of constructing generated IDs.

## Physical transaction and lifetime

Only changed connected groups are replaced. Untouched groups retain their bodies.
After fixed-step synchronization and before simulation, the engine prepares
candidate entities/assets, hulls, native bodies, ID mappings and broadphase
additions before retiring sources. Preparation failure discards staged bodies
and Blast damage. Anchored groups stay static; others become dynamic. Replacements
preserve source origin/rotation/scale, receive their fraction of source mass by
hull volume multiplied by each part's density, recalculate density-weighted
native inertia/center of mass, and inherit
`v + omega × (new_COM - old_COM)` plus source angular velocity. Visual children
are reparented with unchanged local poses. Native raycasts retain part IDs.

Private collider snapshots are not authored assets or global preloads. Roots own
generated bodies and their descendants. Removing/disabling the root, removing
its component, or replacing it with a freshly parsed same-ID entity cancels queued
work and cleans owned fragments at the next fixed step. Scene reset/unload releases
native/world ownership; unused private geometry is retired by physics. Runtime
changes never write scene files. Editing an attached source graph or visual map
requires scene reload; it is not a topology migration operation.

Opt-in `checkpoint`, `restore` and `retire_debris` now support distance/lifetime
policies without respawning destroyed pieces. Checkpoints contain validated
damage and group poses, not mesh geometry. Cleanup retires detached groups through
the native replacement transaction; retained support is not removed. See
[streamed destruction](streamed-destruction.md) for API constraints and the
reusable proximity package. Whole-world streaming/performance is not implied.

This initial transaction copies candidate entity/collider collections. It handles
at most one queued family per step in stable root order. It is synchronous, not
a fair/time-budgeted/resumable scheduler. Large-world copying and temporary
source/replacement overlap require performance/memory qualification. Arbitrary
allocation failure during native commit is not a general rollback guarantee.
`Destruction3D.update` is profiled.

Tests cover world ownership, deferred/partial damage, connected splits, gravity,
retained support collision, repeated splitting, visual pose continuity, inherited
angular/linear motion and mass, budget rollback, removal and same-ID recreation.
A small native body pool tests actual staging exhaustion and cancellation after
broadphase preparation. The desktop lab tests strike, fall, collision and reset.
Lifecycle coverage also runs four full scene split/reset cycles followed by scene
unload, checking generated entities, private collider snapshots and family state.
Runtime attachment tests reject ambiguous colliders, duplicate visual mappings
and persistent visual children.
