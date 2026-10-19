# Water-driven terrain rules

Milestone 5 integration, 2026-10-17. This step connects prepared water coverage
to biome and scatter decisions. It does not add reflection, refraction, waves,
underwater rendering or buoyancy.

## Shared contract

Biome Rules and Scatter Palette accept an optional typed Water input. Connected
coverage replaces the inferred sea-level context: finite elevated lakes can
produce wet ground, while disconnected low ground outside their coverage remains
dry. An explicitly empty water result means dry, not a request to infer an ocean.
Without a connection, the existing recipe-derived sea context remains available.

Unchanged terrain reuses prepared water coverage. A downstream height operation
refreshes coverage against the new ground through the shared water builder,
without carving again or tessellating a water mesh. The incoming heightfield and
upstream water result remain unchanged. Cancellation propagates through this
refresh. Incompatible grid dimensions produce a diagnostic.

Geometry coverage deliberately retains some dry shoreline corners to interpolate
clipped triangles. Those corners must not be used directly as wet gameplay,
biome or material masks. Actual submerged samples require ground at or below
the owning water level. The shared normalization keeps material shoreline
distance, wet substrate, biome rules and early scatter rejection consistent.

Scatter rejects wet candidates before they consume spacing or placement budgets.
Terrain Output retains its final coverage filter after authored terrain edits.
Its Water connection is still required to publish visible surfaces and queries;
connecting Water only to a rule node does not implicitly publish a body.

## Examples and cache behavior

Both terrain examples connect their final Water result to Biome Rules.
`terrain_graph_3d` limits its lowland sediment rule to 12 terrain units from
resolved water. The rocky valley retains its existing biome rules and exposes
the connected coverage for further authoring.

Generator version 12 invalidates derived caches affected by the corrected
material wet mask. Source documents keep their existing format versions;
prepared projects must be recooked. The binary payload layout is unchanged.

## Remaining scope

This closes a rule-integration gap, not Milestone 5. Flow-aware gameplay,
opt-in buoyancy/swimming, animated surfaces and above/below-water rendering still
need implementation and qualification. The
[renderer prerequisite audit](terrain-water-rendering-design.md) records the
shared rendering work required next.

## Qualification

The optimized Linux Release CLI/editor build succeeds. Ten focused Release
suites pass: biome rules, scatter, graph evaluation/cache, material layers,
water generation, water queries, world-space water queries, terrain cooking,
and the two editor graph UI/settings suites. Regressions include elevated
finite water, disconnected low ground, empty water routes, downstream ground
changes, mismatched grids, cancellation, dry geometry corners and a one-placement
budget that must remain available to a dry candidate.

All 32 examples validate; the two changed terrain manifests were reimported
through the CLI. The updated terrain graph example completes a three-frame
headless runtime smoke. This is a focused contract/runtime gate, not a new
water-rendering visual or full repository test qualification.

The maintained terrain guide is deployed in store release
`20261017-water-rule-inputs`, after Composer tests, all 106 Twig checks and
production cache warmup. The activated site's health and new Water-input
guidance were checked. No package contract changed in this step.
