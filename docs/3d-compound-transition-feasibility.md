# Compound transition feasibility

Date: 2026-09-13. This is a tool-level CPU integration proof, not a shipped
destruction feature, engine collider API, or full Android game qualification.

## Implemented and verified

The opt-in `demi-blast-compound-probe` starts with one dynamic Jolt compound plus
a static floor. Selective Blast bond damage produces connected chunk groups.
Replacement compound shapes and bodies are staged before retiring the original
body, between physics updates. Each child inherits the original angular velocity
and the rigid velocity field at its own center of mass:
`v_child = v_parent + omega × (COM_child - COM_parent)`.

Checks cover density-assigned mass, native inertia, conserved total linear and
angular momentum, unchanged shape pose, ray-query chunk identity, gravity and
floor collision. Cancellation after broadphase preparation and a deliberately
exhausted body pool both discard staged bodies and preserve the intact source.
Twenty repetitions of split, unchanged, capacity failure, and cancellation cover
80 world lifetimes. Cleanup verifies zero remaining native bodies; it is not a
heap-leak or sustained-memory qualification.

Both Linux Release and the physical Pixel 7 passed those checks. Linux's four
focused CTests passed in both Debug and Release (Blast, work-boundary smoke, compound, and the
existing Demi-linked singleton handoff). The new probe uses native Jolt directly;
it does not demonstrate the production Demi world/entity/render transition.

## Measurement conditions and results

Host: Ryzen 7 6800H Linux x86-64, optimized `linux-release`. Device: Pixel 7,
Android 15 / SDK 35, ARM64; NDK 29.0.14206865, API 23 Release build. Jolt 5.6.0
and the existing pinned Blast 5.0.6 subset. The Android executable's dynamic
dependencies are libc, libdl and libm; no GPU or PhysX integration is added.

Each target ran separately without concurrent builds/tests. Six cases contain
16/64/256 chunks split into singleton or four-chunk groups, two warmups then ten
recorded runs per case. One Jolt worker, 120 fixed 1/60-second physics steps per
run. A synthetic horizontal chain with box leaves is not a tower, wall, stress
solver, dense debris pile, or imported fracture asset. CPU frequency and thermal
state were not controlled; these short samples are feasibility measurements, not
cross-device rankings or a worst-case guarantee.

Selected 256-chunk results, milliseconds. Phase columns are independent p95s;
do not sum their percentiles as a whole-transition percentile.

| Target | Result bodies | Blast total p95 | Replacement shapes p95 | Stage bodies p95 | Commit p95 | Largest sampled physics step |
|---|---:|---:|---:|---:|---:|---:|
| Linux | 256 | 0.3390 | 0.0164 | 0.0698 | 0.0070 | 1.0141 |
| Linux | 64 | 0.3302 | 0.0304 | 0.0257 | 0.0022 | 0.3789 |
| Pixel 7 | 256 | 0.2797 | 0.1458 | 0.1369 | 0.0125 | 0.7240 |
| Pixel 7 | 64 | 0.2452 | 0.3277 | 0.0714 | 0.0040 | 0.3680 |

`blast_total_ms` includes fixture asset/family preparation, damage, split,
extraction and cleanup; damage/split columns are nested, not additional costs.
Body staging includes `AddBodiesPrepare`; commit removes the source, finalizes
the prepared additions and destroys the source. `physics_total_ms` spans all
120 steps; `physics_max_ms` is the maximum step within a run. `cleanup_ms` only
times body removal/destruction, excluding remaining shape/system teardown.

Peak allocated bodies are 258 for the 256-child case and 66 for the 64-child
case: replacements coexist with the source and floor during preparation. A
production budget must reserve this temporary headroom before accepting work.
Grouping reduces active-body count here but does not make compound-shape creation
free, especially on the phone. These results support separating shape preparation,
body staging and commit in scheduling; they do not establish production budgets.

Raw evidence is retained under `build/compound-transition-linux/` and
`build/compound-transition-pixel7/`: CSV, correctness/stderr logs, executable and
source hashes, device metadata and summaries. The phone's checksummed temporary
executable and staging directory were removed after the successful run. Commands
and rerun instructions are in the [probe README](../tools/3d-feasibility/README.md).

## Remaining work

- The [compound collider contract](collider-assets.md) is now integrated into
  Demi's shared asset/physics path, with stable part raycast IDs and an editable
  `destruction_3d_lab` foundation. The split transaction and automatic render/chunk
  reassignment still need production integration.
- Persistent Blast family ownership, topology generations, queued damage,
  cancellation/stale-result policy and asynchronous scheduling. This fixture
  creates and disposes its Blast family per proposal; it does not roll back a
  persistent Blast actor graph.
  A separate [shared runtime module](3d-destruction-runtime.md) now preserves
  partial damage and stages actor snapshots with commit/discard and state
  revisions. World ownership, queueing and physical commits are still pending.
- Budget synchronous SDK calls and preparation separately; do not pretend the
  split call can pause internally. General allocation failure after commit,
  stress/support analysis, fracture authoring and memory residency remain open.
- Integrate a minimal wall/hammer/rocket scene and qualify visible desktop/mobile
  gameplay, sustained temperatures, memory, response latency and frame-time tails.
