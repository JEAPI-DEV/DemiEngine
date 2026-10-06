# Terrain data and query boundaries

## Investigation

The preceding connectivity work incurred repeated broad builds. Inspection of
the Release dependency index found a path-ID checksum mismatch at byte 7410108:
expected ID 10249, next ID 10251, with a duplicate server-runtime object path.
Ninja repeatedly reported dependency-log recovery. Its
[loader checks](https://github.com/ninja-build/ninja/blob/v1.13.2/src/deps_log.cc)
identify this pattern as conflicting log writers. The historical producer is
unknown; no simultaneous build process was active during repair. The index was
backed up to /tmp and regenerated with `ninja -t recompact`, not hand-edited.

There were also code/build boundary problems. Water tests linked the engine
core, and `TerrainWater.h` included the generator, which included the authored
recipe. Reading generated data therefore pulled in generation/JSON contracts.
Query contexts copied three full coverage vectors and allocated a full zero-flow
buffer even though prepared data already existed. Premature integration builds
during unfinished edits amplified these costs; an isolated algorithm gate should
have preceded the wider integration gate.

## Delivered boundaries

- `TerrainHeightField` owns sampled field data and lattice access. It is
  independent of recipe parsing and generation.
- `TerrainMasks` owns derived sample data without a pipeline dependency.
- `demi-terrain-sampling` builds field access and spatial sampling.
- `demi-terrain-water` builds hydrology, connectivity, appearance and queries.
  Focused water tests do not link the engine core, physics or renderer.
- `TerrainWaterSurfaceBuilder` owns tessellation from already resolved coverage;
  carving and gameplay sampling do not depend on mesh construction.
- Query contexts retain immutable prepared coverage and copy-on-write ground/
  flow snapshots. Caller edits cannot mutate retained data. A missing flow mask
  needs no sample-sized zero allocation; finite flow values are clamped at read.

Native query results now distinguish wet columns from point containment and
expose terrain-local surface/bed elevations. Points in air, on the surface or
below the bed are not underwater. This is native groundwork for the plan's
world-space Lua service and underwater effects, not delivery of those features.
Existing wet-column/depth/flow conventions remain unchanged. No source or cooked
format migration is needed for this boundary refactor.

## Iteration checks

After the repair and target split, a no-change focused build reports no work
(0.090 seconds locally). Touching only `TerrainWaterQueries.cpp` recompiles that
file and relinks the water library/test (1.678 seconds). The isolated water and
query gate passes both checks in 0.02 seconds. These are local build-iteration
measurements, not game frame-time claims or cold-build guarantees.

The broader editor/runtime build still correctly recompiles consumers when
their data headers change. This work does not claim to modularize the entire
core or `LuaScriptHost`. Avoid simultaneous Ninja writers in one build directory;
the maintenance operation repairs existing damage, not future external races.

## Integration qualification

Release editor and CLI build. Eight scoped checks pass in 8.08 seconds: water
world, carving, queries, graph, world-update, generator, terrain surface and
terrain asset. Tests cover immutable coverage ownership, flow snapshot isolation,
surface/bed elevations and above/surface/below-bed point classification, in
addition to existing rendering/query and cook boundaries. The graph example
validates, and source/cooked three-frame headless runs succeed. The final no-change
build reports no work and no dependency-log recovery. `git diff --check` is clean.
No full-suite, Android hardware or new underwater rendering qualification is
claimed.
