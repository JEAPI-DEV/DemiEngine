# Continuous regional terrain sampling

`RegionalTerrainSampler` consumes the existing `TerrainRecipe` and Terrain Graph
contract. Its wrapper supplies only a double world origin and explicit base and
normal sampling steps. There is no RegionLayer authoring language, built-in
world extent, demonstration seed or fixed mountain layout. Asset loading and
runtime residency/publication belong to their respective owners.

The sampler compiles reachable graph nodes into immutable typed instructions.
Noise, constant and landform sources reuse `TerrainEvaluation`, including the
normal recipe seed, graph seed offsets and existing FBm/ridged/warp transforms.
Elevation masks, path/polygon masks, height blend and offset share canonical
point kernels with finite graph execution. Biome Rules shares the finite rule selector. Elevation/slope rules use the
canonical fixed-footprint normal and require a single final Biomes node. Graph node order
and tile load order do not change the result.

The initial sampler-only subset rejected simulation nodes, slope masks and connected
water/instance outputs with node-specific diagnostics. Active sculpt/protection
edits and automatic biome rules require coordinated regional preparation and
are rejected rather than silently ignored. Painted regions remain supported on
the no-graph finite evaluator path; graph biome overlays are explicitly rejected
until their complete preparation contract is shared. A recipe's palette reference
is retained for the vegetation owner, not evaluated by this height sampler.

Tile requests use signed 64-bit indices on one base lattice and a positive
integer stride. Each global point is computed from the same absolute index using
doubles. Region-local coordinates then enter the existing finite float evaluator;
this preserves existing source semantics rather than introducing a second noise
implementation. Shared coarse/fine samples take the same evaluation path.
Tiles return a local `HeightField`, double origin and sample spacing, retaining
recipe biome/material metadata, point exclusions and authored chunk dimensions.

Normals use a fixed world-space centred derivative step independent of tile
stride, including a halo at world edges. Recomputing one-sided normals from
bare tiles would reintroduce seams. Tile evaluation reuses graph scratch storage
instead of constructing JSON or allocating graph values per sample.

The point sampler alone does not solve mesh T-junctions; the regional patch
path now supplies balanced stitching as described below. Scatter now has the global candidate windows described below; regional
environment preparation must still supply matching boundary measurements. Independent
per-tile erosion is absent; future erosion needs a regional/supertile boundary
authority.

`demi-terrain-region-tests` compares supported authored graphs and no-graph
recipes with canonical finite generation, including source seeds and spatial
mask falloffs. It also checks tile edges, shared coarse/fine heights/normals,
load-order and graph-order independence, large origins, authored graph tuning,
outer-edge halos and rejection diagnostics. No rendered quality, streaming or
performance qualification is claimed before coordinated builds and measurements.

## Global scatter candidate windows

`TerrainScatterSettings.domain` is an explicit native foundation, not a new
JSON or vegetation storage policy. `TerrainScatterDomain` owns world bounds,
local field origin and a half-open global ownership window. A field can include
an environment/sampling halo outside its ownership. Output positions and sample
cell indices remain local; candidate coordinates and durable rule/site IDs come
from the whole-world lattice.

Selection, jitter, yaw, scale and spacing use the same world-wide potential
sites regardless of which window is loaded. Candidate iteration visits only
the ownership range plus displacement/rounding margins; spacing consults global
potential neighbors beyond that range. Clustering evaluates global positions.
An explicit domain requires supplied matching environment constraints and a wet
grid, including all-dry grids. It never infers regional hydrology from a tile.
Callers must provide consistent boundary measurements and interpolation samples.

Without a domain, finite scatter preserves its previous arithmetic and boundary
behavior. Global mode owns [minimum, maximum) windows, including the outer world
edge policy. Partition equality requires untruncated results, consistent seed/
palette/world bounds, and matching environment/height inputs. The native `TerrainVegetationResidency` owner now retains asynchronous cell
results and retires them on eviction. `TerrainVegetationWorld` publishes these
records through ordinary render batches and prefab ownership, with native
physics eviction tests. Automatic scene traversal and regional environment
preparation are still pending.

## Authored assets and qualification

The `terrain_region` DataAsset stores a reference to an ordinary Terrain asset,
plus origin and sampling spacing. It does not contain a parallel layer language.
CLI source validation compiles referenced authored graphs and reports unsupported
regional operations. Cooked graph compilation/loading is not implemented by this
source helper; it is not the future runtime streaming contract.

The Assets panel's **Open referenced terrain** action uses the existing Terrain
workspace, graph and undo/save path. Example definitions are below
`examples/terrain_3d/assets/terrain/regions/`; their scene-specific numbers remain
in authored JSON. Native configuration contains no Alpine seed or region extent.

Four Linux Release suites passed: terrain graph, spatial masks, regional sampler
and regional asset parsing. They include finite parity, shared borders, normal
continuity, unsupported-node diagnostics, authored-reference validation and
rejection of sampling steps below the underlying coordinate precision.
The example project validates 189 files without diagnostics. This establishes
shared sampling and authoring, not a functioning 53,000 km2 streamed world.
See [the streaming plan](world-terrain-streaming-plan.md) for remaining features
and the measured vegetation regeneration cost.

## Runtime extension (2026-10-25)

`RegionalTerrainSystem` now owns a dyadic rectangular cover with 2:1-balanced
neighbors and stitched edge samples. It retains the current cover until every
replacement patch in a split/merge is ready. Camera motion can replace a pending
plan, including one stalled by its admission budget. A stationary settled camera
does not repeatedly rebuild the cover plan. Meshes and native collision shapes
prepare on workers; live ownership changes and adoption stay on the runtime
thread. Evicted native bodies are removed even when simulation is paused.

`RegionalTerrainVegetation` uses the shared palette/scatter and prefab
prepare/adopt paths. Final-field scatter is supported; moisture/water-distance
requirements remain rejected. Cell candidate keys span the whole region and
ownership is half-open. Tests cover deterministic eviction/revisit, replacement
owners, cancellation, adjacent edge interpolation and collision coverage.
Retired services drain CPU jobs without revisiting live ownership belonging to
a replacement region.

Regional material assignment supports elevation/slope rules through the shared
`TerrainBiomeRuleEvaluator`; the earlier automatic-biome limitation above no
longer applies to that subset. Connected regional water/simulation, arbitrary
upstream scatter branches, edits, and authored stored-placement transport remain
unsupported. Source asset and overview preparation must never populate the
whole region. The source-resource regression checks both missing finite caches
and small retained program payloads.

## Viewer LOD correction (2026-10-31)

The editor passes its own viewer position and direction to the shared regional
selector. Distance now uses each resident patch's sampled vertical bounds;
using one height underneath the camera misclassified nearby elevated terrain.
Refinement prioritizes nearby patches, weighted toward the viewing direction,
before distant coarsening. Whole-transition admission checks avoid requesting a
split that cannot fit alongside its active cover. Distant eligible merges can
make space without discarding the current visible cover.

Linux Release regressions cover elevated foreground priority, stationary
mountain-cover convergence, translated terrain, and stable publication under a
16 MiB cache budget. Editor publication also invalidates the hierarchy projection
when entity order changes: retaining borrowed entity pointers across streaming
publication caused a crash in the visible editor probe. These checks establish
selection/lifetime correctness; they do not establish seamless visual LOD morphing.
