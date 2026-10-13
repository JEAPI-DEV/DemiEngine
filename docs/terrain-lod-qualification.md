# Terrain LOD qualification

## Follow-up: demand-driven render resources, 2026-10-12

The current compatible renderer uses GPU-displaced shared patches; see
[GPU terrain qualification](terrain-gpu-rendering-qualification.md). Ordinary
mesh fallback no longer retains every reduced level eagerly. Chunk descriptors
retain bounds, errors, counts and immutable source identity; a bounded worker
queue prepares only requested reduced levels. Full meshes and collision remain
available during preparation, failures, cancellation and budget pressure.

Ready resources are stable throughout a presentation's camera/shadow views.
Material snapshots can share geometry without repeatedly cancelling each
other's jobs. Terminal failures do not spin idle workers. Scheduled render
targets retain demand between updates; hidden editor views cancel unused work
and release resources without redrawing their retained image targets. Inactive
maintenance reaps completed cancellation on idle timeouts.

The CPU LOD payload cache and GPU-field staging queues have configurable
allowances, with full source/collision memory and allocator overhead excluded.
GPU field textures and shared topology have a separate budget. Zero preparation
jobs pauses new work while valid ready resources remain usable. This is render
resource residency, not the remaining large-world/gameplay streaming milestone.

## Initial live render integration, 2026-10-12

Runtime and editor rendering now consume immutable chunk LOD preparations.
Terrain loading prepares the reduced meshes and exact geometric errors; local
publication rebuilds affected chunks, and material rebinding retains geometry.
The Full meshes and colliders remain independently available. Shared asset
placements share prepared data on loading, with separate camera/placement
selection history.

`Terrain3D.lod_error_pixels` defaults to 2. Zero selects Full detail. Reflection,
validation, schema, serialization and the Terrain Inspector expose the same
field. Direct quality edits preserve generated resources; generic editor
Undo/Redo still uses the existing scene rebuild path and restores the value.

Selection projects transformed local-height error using camera-space chunk
bounds. This avoids inflating error with unrelated horizontal scale. The budget
uses originating presentation viewport pixels; downscaled render targets remain
conservatively constrained, and shadow atlas dimensions do not set quality.
Every biome in a chunk shares the decision. Coarsening requires 15% headroom;
refinement occurs immediately when the error budget is exceeded.

The GPU cache retains demand across all cameras and shadow views in a
presentation. Unused entries retire after eight presentations. Reduced geometry
shares GPU buffers across placements; material bindings remain per draw. Failed
reduced uploads fall back to Full without replacing collision or destroying an
existing cache entry. Profiler gauges expose selected surface counts, terrain
triangles, resident GPU bytes/meshes and uploads.

The graph example's camera far clip is now 5,000 units instead of 500. Its
landscape spans 2,000 units; the old clipping plane cut the view off during the
camera-height sweep. Render LOD does not impose a terrain eviction distance.

### Moving-camera evidence

Run `scripts/benchmark_terrain_lod.py` with an optimized binary, a unique output
directory and `--build-configuration linux-release`. The script creates temporary
project copies, uses identical frame-indexed camera paths, disables VSync for
measurement, and records per-frame CSV plus binary/source hashes. It does not
override simulation delta time or modify the example in place.

At 2,954 by 1,744 pixels, 600 frames with 60 warmup frames excluded, and the
5,000-unit far clip:

- The default 2-pixel budget retained Full terrain throughout this path. Both
  runs submitted a median 178,896 terrain triangles. Timing variation between
  these runs is not evidence of a LOD improvement.
- An explicit 8-pixel diagnostic reduced median terrain triangles from 178,896
  to 144,028 (about 19.5%). CPU render preparation changed from 6.41 to 6.19 ms,
  and frame time from 9.20 to 8.76 ms. This is a quality tradeoff diagnostic,
  not the default setting or evidence that triangles dominate frame cost.
- The field has 256 chunks and 4-unit sample spacing. Cached Half errors range
  from 1.84 to 24.54 terrain units, with median 3.47. Full fallback at the tighter
  budget is expected for this coarse, rough surface and camera path.

Reports for this session: `/tmp/terrain-lod-tight-long-view/` and
`/tmp/terrain-lod-eight-pixels/`. Temporary reports are not durable repository
artifacts; the script and recorded workload provide reproduction.

### Remaining scope

This delivers render LOD and GPU mesh residency, not large-world streaming.
All heightfield samples, Full CPU meshes and collision remain resident. Preparing
LOD adds loading/edit work and retained CPU geometry. Large chunk preparation
can delay cancellation until the current geometry/error operation finishes.
CPU paging, actor-driven collision residency, layered PBR materials, vegetation
instancing and advanced water remain separate terrain-plan gates.

### Live qualification

Thirteen focused Release CTest suites passed, covering component/schema contracts,
LOD geometry and preparation, renderer selection/cache behavior, visibility,
terrain publication and editor history/workspace integration. The graph example
validated all 15 reachable files and completed a three-frame headless run.
`git diff --check` passed. This is not a full repository or Android-device gate.

The rebuilt native editor was launched at 5,120 by 2,806 pixels. The Terrain
Inspector displayed the default 2.000-pixel setting, and embedded Play displayed
the distant mountain range with the extended far clip. Session captures are
`/tmp/terrain-live-editor-view.png` and `/tmp/terrain-live-editor-play.png`.
The native screenshot is visual qualification, not a controlled performance
comparison; the moving-camera runs above provide the repeatable measurements.

## Initial geometry pass, 2026-10-11

The initial change repaired native LOD geometry and residency-policy helpers
before connecting them to live rendering. At this checkpoint camera-driven LOD
was not yet enabled. Full scene meshes, collision data, authored terrain
documents and the cooked format were unchanged.

## Correctness changes

The previous error calculation compared each source height with a coarse
lattice corner. A perfectly planar slope therefore appeared to have geometric
error. The new calculation compares the height planes of the Full and reduced
triangles over their intersection. Testing intersection vertices includes
crossing diagonals and boundary fans; checking only original grid samples would
miss some maxima. Polygon clipping uses fixed stack storage rather than heap
allocations for every triangle pair.

Reduced chunks retain every original perimeter sample. Boundary triangles split
to meet those samples, so different levels share actual edge segments, including
peaks and valleys between coarse samples. Shared heightfield normals keep the
lighting boundary consistent. Optional skirts remain supplemental geometry;
they are no longer the mechanism relied upon to cover missing edge peaks.

Full-detail biome grouping now uses the same per-triangle ownership rule as
scene publication. Reduced groups vote the owners of Full triangles instead of
raw labels, preventing an isolated sample from introducing a new surface group.
Both paths share the terrain-local distance times biome texture-scale UV
calculation. Explicit sample and triangle provenance replaces assumptions about
the flattened vertex lattice.

Streaming budgets count the refined perimeter triangles. Retaining a distant
chunk at the configured resident level no longer bypasses geometric-error
selection. Both policy queries validate nonfinite inputs consistently. Their
positions and distances are explicitly terrain-local; these helpers do not
apply scene transforms.

## Integration gate still open

Exact geometric error is preparation work. The reference selectors still
rebuild topology and scan source geometry when called, and the streaming
evaluator still validates tiling, allocates state and formats diagnostic text.
None belongs in an unchanged terrain's per-frame selection path.

Live integration must prepare immutable errors and render variants per terrain
revision, invalidate only affected chunks on edits, and retain Full collision
independently of render detail. It must also qualify transformed terrain,
multiple cameras, shadow-only demand, transactional replacement and GPU resource
retirement. Residency history needs explicit terrain/revision ownership rather
than matching chunk rectangles alone. CPU streaming and gameplay collision
residency are separate work from selecting and retaining GPU render meshes.

No frame-time improvement is claimed from these helper repairs. Native visual
inspection was deferred because the user was away and desktop input permission
was unavailable.

## Validation

The optimized Linux Release CLI and affected test targets built successfully.
Assertions remain enabled in the test executables.

- Eleven selected CTest suites passed: LOD geometry, biome grouping, publication
  parity, streaming policy, terrain patches, surface queries, material layers
  and blending, world loading, world updates and shared asset publication.
- Geometry regressions cover every mixed-level pairing along both axes, shifted
  odd-sized chunks, narrow chunks, exact perimeter segments and shared normals.
  An explicit crossing-diagonal case has maximum grid-sample error 8 but actual
  surface error 25/3; the reported bound includes the fractional intersection.
- The publication test compares oriented triangles, normals and UVs directly
  against `terrain_detail::buildChunkTriangles`, including mixed biome ownership
  and different texture scales. This tests the existing live builder contract,
  not a second test-only definition of its expected output.
- `demi validate examples/terrain_graph_3d` checked 15 files without diagnostics.
  The project completed a three-frame headless run with 1,229 renderable entities.
- `git diff --check` passed. This is a focused qualification, not a full
  repository or interactive GPU test gate.
