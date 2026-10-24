# Highland terrain and organic scatter

2026-10-23. This extends Silvermere as an executable finite-terrain probe.
It does not claim Unreal Engine 5 feature or visual parity.

## Reproduce

Run the Release demo with `./build/linux-release/demi run --project examples/terrain_3d`.
Space pauses, R restarts, N selects the next chapter, and M changes landscapes.
M loads a lightweight loading scene, prepares the other landscape, and activates
it with ordinary scene replacement. Returning unloads the highland scene rather
than retaining both full terrain worlds. Alternatively open
`scenes/mountain_range.scene.json` in the editor and enter Play.

The new terrain is 2048 x 2048 metres, 1024 x 1024 cells (2 m spacing), 32-cell
chunks, a 6 km camera far plane and a 98-second seven-chapter route. Shadow range
is independently 180 m. Heightfield ridges, drainage-fed erosion and finer negative offsets produce
actual geometric relief. The highland valley opens through opposite field edges
instead of making another closed circular basin. The first overly aggressive
fine-cut settings produced artificial needle shapes in native inspection and
were reduced before retaining the example. This remains a heightfield: no caves
or overhangs. Palette and sampling settings are authored, not baked screenshots.

The route uses the same camera-path package as Silvermere. Highland camera
positions are clamped above physical terrain by a downward query; chapter tests
check X/Z positions and upward-only clearance. This avoids assuming that an
interpolated path authored above one terrain also clears every regenerated ridge.

## Scatter change

The previous implementation randomized scale/yaw but kept every position on the
candidate lattice. At Silvermere's nearly full grass occupancy that made visible
rows. `position_jitter` in [0,1] now controls deterministic independent X/Z
placement within half a candidate cell. Default zero preserves deliberate grids;
all nine organic example roles use one. Spacing evaluates these same displaced
potential sites. IDs, independent variation streams, resolution independence and
local filtering behavior remain deterministic. This is stratified randomness,
not a Poisson-disc packing promise or automatic full ground coverage.

Cluster strength and scale variation increase for groundcover and rocks. The
large fixture uses lower physical densities to keep finite-world placement work
bounded. Grass still does not cast shadows; trees and other vegetation retain
their authored casting. Ground material tint is less saturated. Old placement
counts and old performance tables in the Silvermere record are historical.
Generator/payload versions are 15/13: source caches regenerate and cooked terrain
must be recooked, rather than interpreting old bytes with the new palette layout.

## Memory admission

The first native highland transition exceeded the implicit scene group's 64 MiB
decoded-memory default. The fix adds an authored group budget for this scene:
`groups/highlands.asset-group.json` selects 128 MiB decoded, 384 MiB resident,
and 3 ms upload work per frame. Defaults for other scenes remain unchanged.
Matching reserved scene-group manifests customize budgets while automatic
scene asset discovery remains authoritative. Errors identify the asset, group,
accounted bytes and limit. These loader counters do not account for all process
memory, render geometry, physics allocations or driver allocations.

The subsequent [memory investigation](highlands-memory-investigation.md)
measured the missing breakdown: most post-return RSS was already-free allocator
storage, driven in part by large temporary prepared-terrain JSON decoding.

## What loading and unloading mean here

Terrain render LOD is screen-error driven; renderer residency can retire reduced
meshes and complete unused GPU fields. CPU terrain geometry and collision are
not paged by camera distance. GPU fields are whole height/normal/classification
allocations, retained while any patch from the field is demanded. This is not
large-world disk streaming or texture paging.

The native transition capture after the memory fix measured highland GPU field
residency at 25,215,320 bytes (about 24 MiB), returning to Silvermere's 3,557,720
bytes after replacement. The highland segment used up to 869 terrain patch
instances with zero patch fallbacks. This transition/chapter-seek capture is a
functional stress check, not a fixed-camera performance benchmark. Process RSS
peaked around 5.1 GB and remained around 4.7 GB after the return; scene removal
is not a claim that every preparation cache or allocator allocation returns to
the OS. Those are pre-compaction measurements. The subsequent compact-placement run
reduced sampled peak RSS to about 1.92 GiB and post-return RSS to 1.60 GiB; see
the memory investigation for method and remaining limits. Whole-scene CPU
preparation and retained memory still remain scaling limits.

A renderer regression uses two independently populated planar fields with camera
A -> B -> A. It checks reduced and Full LOD, texture retirement/rebuilding,
zero fallback and actual SceneFlow unload releasing entities and ownership.
Shared immutable heightfield pages can legitimately share GPU channels; the
fixture uses different height data so its retirement assertions test ownership
rather than forbid valid sharing.

## Validation

All six focused palette, scatter, prepared-asset, cache, renderer and runtime
asset-service suites passed together (0.92 seconds). Budget tests prove that the default remains enforced, authored limits
apply through both preparation paths, unrelated scenes retain defaults, and
invalid reserved roots fail validation. The native Linux E2E adds a full
Silvermere -> highland traversal -> Silvermere test to the existing tour controls.
The final art-parameter native rerun passed all three E2E tests, including
chapter controls, highland loading/traversal and return. The project validates
185 source files without diagnostics. These are focused checks, not a full
repository or Android qualification.
