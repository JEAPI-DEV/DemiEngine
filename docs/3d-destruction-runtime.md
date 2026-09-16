# Destruction runtime integration

Milestone 3 is in progress. The arch example still demonstrates one compound
body, not a destructible wall. Mouse part picking is now manually verified.

## Persistent Blast damage state

`runtime/destruction/BlastFamily3D` is an internal C++ module linked through the
shared graphical/headless runtime dependencies. It owns the Blast asset and
family without exposing SDK pointers or indices to other engine subsystems.
The [compound collider format](collider-assets.md#optional-fracture-graph) now
supports authored bonds and anchors. `ColliderFractureFamily3D` bridges a loaded
collider snapshot to this module and derives chunk volumes/centers through Jolt.
Focused native tests are its current consumer. There is no world attachment
component or Lua destruction API yet; runtime world ownership is still pending.

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
anchored chunk; this flag does not yet pin a Jolt body or run structural stress
analysis. There are no virtual-world bonds or runtime anchor changes yet.

## Staging contract

1. `stage` accepts a bounded batch of unique bond IDs and finite positive damage.
   It snapshots every live actor through Blast's supported serialization API
   into a separate family, preserving previously committed partial damage.
2. Blast applies damage and splits that staged family. `stagedGroups(token)`
   exposes sorted stable chunk IDs plus anchor state. The live family remains
   unchanged, including its health values.
3. The eventual physics coordinator must prepare replacement shapes, bodies and
   visual ownership **before** accepting this proposal. Failure or cancellation
   calls `discard(token)`; successful physical preparation will call
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

Next: world ownership and complete visual fracture assets, then transactional Jolt body
replacement/render reassignment, spatial damage resolution, bounded scheduling
and the visible wall/hammer/rocket probe. Milestone 3 is not complete.
