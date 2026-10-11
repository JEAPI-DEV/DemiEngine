# Terrain LOD geometry qualification

## Scope, 2026-10-11

This change repairs native LOD geometry and residency-policy helpers before
connecting them to live rendering. It does not enable camera-driven LOD in the
editor or runtime. The existing full-resolution scene meshes, collision data,
authored terrain documents and cooked format remain unchanged.

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
