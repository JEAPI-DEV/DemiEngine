# Terrain Milestone 3 qualification

Date: 2026-10-14. Native build: `linux-release`, GCC 16, Linux.
Generator version: **9**. Prepared terrain asset payload: **7**.
The separate cooked heightfield format remains 3. Authored format versions and
example recipes did not change. Old derived output must regenerate/recook.

## Scope and acceptance

Milestone 3's bounded base-generation scope is complete. This does not close
Milestones 4–8 or claim reference-game visual fidelity. Eight built-in starter
graphs now cover flat ground, dunes, hills, mountains, basins, plateaus, coastal
islands and a drainage-connected river. Shared ridge/warp/directional controls
and editable path/polygon masks compose macro shapes and smaller variation.

The existing rocky-valley recipe retains its 500-by-500 cells over 2,000-by-2,000
units. Sequential native Vulkan views at 3413-by-1917 checked broad ridges,
the curved valley and a close valley-floor view. A separate starter probe
checked the descending routed channel joining its ocean at wide and close
views. Shadow casting was disabled only in a temporary rocky-valley copy to
separate shape from the already deferred cliff-shadow artifact; repository
lighting and source resolution were not reduced.

Temporary captures (not shipping assets):

- `/tmp/terrain-valley-wide-v9.png`: original wide camera, corrected generation.
- `/tmp/terrain-valley-close-v9.png`: close valley-floor shape after correction.
- `/tmp/terrain-valley-close.png`: historical artificial sediment walls.
- `/tmp/terrain-routed-river.png` and `/tmp/terrain-river-close.png`: starter
  channel and ocean connection.

These show coherent base shapes, not finished terrain art. Coarse material
patches, triangle-scale river edges, missing vegetation and the deferred shadow
artifact remain visible limitations. Full reference-scene comparisons belong
to the later visual milestones.

## Drainage, routing and water ownership

Priority flood uses the six real edges of the canonical terrain triangulation.
An opposite diagonal cannot escape through an intervening high triangle edge.
Filled elevation is exact rather than incremented by a flood epsilon; large
flat fields therefore do not accumulate fictitious depression depth. Reverse
traversal accumulates all upstream flow before propagation to the parent.

The separate route tracer validates guidance and follows nonascending original
surface edges. Its explicit endpoints are sea level, field edge, closed basin
or blocked surface. It does not mistake a filled-surface uphill flood parent
for a physically downhill river. The graph adapter adds optional existing-water
joins, preserving previous water when the source is already wet or a new route
cannot be placed. Body IDs remain graph-node IDs.

Follow-bed rivers use interpolated authored bed height plus positive water
depth. At receiving water, a bed floor prevents the joining surface from falling
below the receiver; this can form a flat backwater reach. Carving, wet coverage,
clipped mesh vertices, normals and gameplay queries share that profile. Each
body owns its levels so overlapping bodies cannot interpolate each other's
heights. Runtime normals use the full terrain transform, including nonuniform
scale. Constant-level rivers remain available for deliberately level bodies.

Typed graph dependencies give the order: landform/erosion, receiving water if
needed, drainage, routed carving, then downstream rules and output. There is no
implicit refinement loop. Generation works on the complete field before chunk
partition. Tests compare 7-cell partial chunks with 16-cell chunks: heights,
base, biomes, drainage, route chains, authored river paths and water queries are
identical, including every grid column and half-sample probes along chunk edges.
The existing erosion test independently checks changed chunk partition.

## Hydraulic correction and accounting

The earlier solver followed stale drainage and dumped terminal sediment,
creating artificial walls. Generation 9 chooses currently descending mesh
neighbors, exports carried sediment at open field edges, and caps deposition by
local talus capacity. A terminal droplet can settle into its immediate mesh
neighborhood; any remainder becomes explicit aggregate unfinished transport.
It is not secretly dumped onto one sample.

Graph results and prepared assets retain gross hydraulic totals for eroded,
deposited, exported and suspended material. Units are summed sample-height
changes, not physical mass. Suspended transport is **not persistent fluid state**.
The representative run retains about 47.4% of gross erosion as unfinished
transport; this is a material limitation of the bounded algorithm. Net cut and
deposition masks include thermal relaxation and differ from these gross totals.

On the unchanged 501-by-501 pre-erosion field, with 250,992 hydraulic droplets:

| Stage | Two sequential native runs |
| --- | --- |
| Hydraulic | 211.66–218.70 ms |
| Thermal | 47.73–47.83 ms |
| Combined | 261.43–273.74 ms |

These are generation stages, not render-frame times. There were no other
engine instances, builds or tests during measurement. Hydraulic/combined output
changed, so this is not a pure optimization comparison. Thermal's digest stayed
identical while its working-buffer representation became contiguous.

The largest combined deposit changed from about 272.18 to 24.65 world units;
the largest interior hydraulic deposit changed from about 252.67 to 24.95.
The hydraulic transport ledger closes exactly on this probe. Combined field
accounting differs by 0.00574 summed height units after thermal float rounding.
These are measured examples, not general bounds for all recipes.

The [erosion evaluation](terrain-erosion-library-evaluation.md) contains pinned
HighMap/soillib surveys and actual libtcod comparisons on four identical input
fields. Native remains the integrated solver because it owns engine grid,
progress, cancellation and artifact contracts. No third-party erosion dependency
was added to the engine, and no claim of superior native visual quality or
cross-platform bitwise repeatability follows from this Linux comparison.

## Validation and limits

Release runtime/editor rebuilt. All 24 focused suites passed: drainage, route,
erosion, river graph, sediment masks, landform controls, water, water runtime,
water queries, water mesh, water world, assets, asset pipeline, cook, presets,
CLI, graph, pipeline, cache, and editor terrain authoring/workspace/assets/
history/graph UI. The added route partition regression also passes separately.

Coverage includes closed/shallow basins, physical edge connectivity, downhill
and existing-body joins, transactional cancellation, invalid numeric ranges,
source immutability, bounded settling and ledger closure, profile overlap and
transforms, cache/replay retention and prepared cold-load roundtrips. Earlier
[Preview/Final qualification](terrain-generation-quality-qualification.md)
records workspace rollback/history and Final-only save/play/cache/cook guards.

`rocky_valley_3d` (11 files), `terrain_graph_3d` (15) and `terrain_3d` (26)
validate without diagnostics. The rocky-valley three-frame headless smoke passes.
No physical Android/Windows qualification, new mouse-driven editor interaction,
package upload or website deployment is claimed here. PHP is unavailable in
this environment, so website doc verification and Twig lint could not run.
Native captures used the
runtime; editor command behavior is covered by the focused workspace suites.

Automatic watershed-source selection, dynamic flooding, time-dependent fluid
simulation and a fully settled geological sediment simulation are not delivered.
The public River Route requires an authored source and explicitly reports a
closed/blocked endpoint. Material integration is the next terrain milestone;
shadow work remains deferred at the user's request.
