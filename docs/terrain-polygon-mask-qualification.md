# Terrain polygon mask qualification

Date: 2026-10-13. Build: `linux-release`.

## Delivered contract

`polygon_mask` selects a closed terrain-local X/Z outline. Interior and boundary
samples have weight one; only the exterior fades using smoothstep over a
nonnegative world-unit falloff. Zero falloff is a hard edge. The final point
connects automatically to the first. Concave outlines, either winding, repeated
points and explicit closing points are supported. Self-intersections use even-odd
filling. At least three noncollinear points are required; coordinates must be
finite and within float range. Calculations use double precision.

`TerrainGraphSpatialMasks` owns shared point parsing, validation, segment
distance and cancellable sample evaluation for polygon and path-distance masks.
The graph parser delegates validation and adds node identity to errors. Existing
path corridor semantics are preserved. There is no parallel editor shape model:
metadata and schema describe the node, and the existing bounded structured point
controls edit it through graph commands and history.

Height Blend composes the mask with ordinary flat/noise fields. Basin replaces
a noisy region with a low flat floor; Plateau raises a flat top; Coastal Island
blends a -6-unit seabed toward raised noise and adds an ocean at zero elevation
plus a shore biome. These new built-in presets use a 128-by-128 extent and a
64-by-64 grid, contain editable explanatory comments and require no imported art.
They are reusable graph compositions, not separate specialized generators.

## Verification

Eight focused suites pass: spatial masks, graph execution, presets, CLI,
graph-landform controls, graph UI, graph document and terrain history. They cover
winding, concavity, boundaries, repeated points, self-intersections, exterior
falloff, malformed/overflowing coordinates, cancellation, source immutability,
valid normals, generated interior heights, ocean publication and cache reuse.
Graph UI tests include polygon points across canvas sizes and zoom levels.

CLI tests also exposed a previously omitted `landform_warp` seed channel in
inspect/seeds output. The reporting table now includes it; derivation and terrain
output are unchanged. Preset listing checks now cover seven built-in entries.

CLI preset application was exercised on three temporary inline-recipe projects,
then each project validated without diagnostics (two authored files per project).
Native Vulkan/X11 runs were sequential, with one active instance at a time,
and captures at 1600-by-900 showed the intended basin floor, plateau top and
island coastline/ocean. Temporary evidence:
`/tmp/terrain-preset-basin.png`, `/tmp/terrain-preset-plateau.png`, and
`/tmp/terrain-preset-island.png`. The Release editor/runtime were rebuilt.
No desktop mouse interaction or physical Android rendering is claimed here.

## Scope and remaining work

These controls add authored macro shapes. They do not derive closed hydrological
basins, choose river outlets or simulate coastline erosion. Detailed materials,
vegetation, improved water shading and the reference-scene visual gate remain
open. The rocky-valley and sculpted terrain sources were preserved. Shadow work
is deferred at the user's request. Public website templates are updated locally;
no online deployment or package publication was performed.
