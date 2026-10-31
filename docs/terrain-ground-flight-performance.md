# Terrain performance while moving through vegetation

2026-10-31, Linux Release. This follows the settled opening-view measurement in
[streaming stability](terrain-streaming-stability.md). That stationary result
must not be presented as traversal performance.

## Workload

`scripts/profile_terrain_flight.py --output /tmp/terrain-flight --seconds 130`
launches the native editor maximized, copies the user's dock layout into private
state and runs a private copy of `examples/terrain_3d`. The authored project and
saved workspace are unchanged. Pass the baseline artifact's `*-workspace`
directory with `--layout` to freeze docking across comparisons. The runner rejects
mid-flight Game View resizing. It installs
`scripts/probes/terrain_ground_flight.lua` into that copy only.

The camera follows collision height at **3 m above the ground**, from X/Z (0,0)
to (1200,800) along a shallow curved path and back. It waits for terrain collision,
settles for 10 seconds, travels outward for 25 seconds, pauses for 10 seconds,
returns for 25 seconds, then settles. The trace marks each phase with
`Bench.flight.*`. The profiling tool rejects missing rendered phases or a route
without resident vegetation. Shader quality, draw distance, vegetation density,
MSAA, shadows and render scale are unchanged between comparisons.

The measured window is 5120 × 2742; its Game View is 4076 × 1746. Movement phases
retain median 74 cells outward and 81 returning, with 83 at the far pause. The
main view examines over 100,000 instance candidates near the opening. These are
candidate counts, not a claim that every candidate survives culling.

An earlier climb to 3 km altitude was discarded as evidence for vegetation
traversal after the user pointed out that it left vegetation range. Its result
is not included below. The first ground run's final returned/idle tail overlapped
the subsequent build; only its completed outbound, far pause and return phases
are used for comparison. No builds ran during those phases or the final capture.

## Changes

- Terrain extraction previously sorted chunks inside each owner. Regional
  terrain has a separate owner per chunk, so this left the landscape in
  publication order. `TerrainPatchBatch3D` now stably sorts the complete opaque
  patch set by view depth before forming compatible groups. Both camera and
  shadow views use their own direction; shadow LOD selection still uses the
  originating camera. Equal-depth ordering, instancing, topology, materials and
  partial-submission fallback ownership remain intact.
- Regional render-request refresh previously called linear `findEntity` for
  every active patch, making each cover change scan the world repeatedly.
  It now uses the existing `EntityLookup`. Split transitions no longer append
  a duplicate target cover to active GPU demand, and a completed merge does not
  rebuild identical requests twice. A fully resident, unchanged patch set skips
  rebuilding the same residency demand on subsequent fixed ticks; incomplete,
  pending, retiring and budget-blocked demand continues polling. The new
  `Terrain.region.residency_update` scope identifies actual cache work.
  The render-readiness barrier remains in
  place; no transition starts early to improve timing.
- New regression coverage checks ordering across independent GPU fields,
  stable ties and shared-field batching, and unique active requests during
  streamed splits. Existing camera/shadow, resource lifetime, split/merge and
  fallback tests exercise the surrounding contracts.

## Measurements

The ordering-only intermediate capture reduced outward median GPU time from
21.21 to 16.33 ms and return GPU time from 21.42 to 16.43 ms. CPU time remained
significant; this intermediate result was not claimed as an equivalent total
frame-time improvement. A subsequent capture resized its Game View from 4076 × 1746 to 4076 × 1258
mid-flight; its apparent improvement is discarded. The matched repeat restores
the original dimensions. Final measurements after the residency optimization, median / p95 milliseconds:

| Phase / metric | Before | Final |
| --- | ---: | ---: |
| Outbound GPU | 21.209 / 23.890 | 16.069 / 19.645 |
| Outbound Game CPU | 24.999 / 37.538 | 25.406 / 37.322 |
| Outbound editor frame | 27.452 / 39.888 | 27.937 / 39.905 |
| Far pause GPU | 21.855 / 22.993 | 16.648 / 18.845 |
| Far pause Game CPU | 25.261 / 39.011 | 24.344 / 34.630 |
| Return GPU | 21.420 / 24.835 | 15.915 / 19.481 |
| Return Game CPU | 27.358 / 40.639 | 28.672 / 41.405 |
| Return editor frame | 29.920 / 43.174 | 31.159 / 44.232 |

Both compared runs retain exactly 4076 × 1746 Game Views throughout every phase.
Median instance candidates are 110,064 / 109,994 outward and
111,796 / 111,804.5 returning (before / after); residency remains 74 / 81 cells
respectively. Thus the GPU improvement does not come from flying out of foliage
range or reducing resolution. The final runner also successfully rejects the
resized intermediate capture as invalid.

GPU cost falls about 24–26% during movement, but total movement frame time does
**not** improve consistently. CPU instance extraction/submission and publication
spikes remain significant. No general FPS gain or smooth-traversal claim follows
from this GPU result alone. The earlier split-request cleanup targets wasted
CPU work; it is not evidence that all main-thread bottlenecks are resolved.

Focused renderer/batch and regional streaming/morph tests pass after rebuilding.
The visible four-scenario E2E suite passes again with the final residency fast
path, covering traversal, teleport/revisit and scene unload. Project validation
checks 206 files with no diagnostics, and `git diff --check` is clean. These are
focused checks, not a full repository test gate. Final E2E evidence is in
`/tmp/demi-ground-qualified-e2e.log`.

Artifacts: `/tmp/demi-ground-before.csv`, `/tmp/demi-ground-sorted.csv`,
`/tmp/demi-ground-matched.csv`, `/tmp/demi-ground-qualified.csv`, and their logs/screenshots/summary JSON files.
GPU timestamps are delayed and use a separate clock from CPU scopes. The runs
share a route and source content, but asynchronous generation can change which
exact frame publishes a chunk. Percentiles are descriptive single-run results.
