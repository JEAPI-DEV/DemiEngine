# Terrain landform qualification

Date: 2026-10-13. Native build: `linux-release`.

## Delivered boundary

`TerrainLandform` owns parsing, canonical serialization, validation and shape
identity for recipes, presets and graph noise nodes. FastNoiseLite provides the
FBm/ridged and BasicGrid domain-warp implementations. Directional stretch and
heading orient the sampled coordinates; independent named warp seeds keep
coordinate displacement separate from elevation noise. Default settings retain
the previous FBm samples and canonical representation. Domain validation guards
finite heights, rotated/stretched coordinates and the library's integer lattice.

`TerrainGraphPathMask` evaluates rounded polyline corridors in terrain-local
units. Width is a half-width; falloff is a smooth transition outside the core.
Repeated points and points beyond terrain boundaries are supported. The output
retains its immutable source field and cancellation is checked during evaluation.
The graph editor exposes structured point rows and the ordinary reversible
parameter-edit path. Shared structured vector inputs now retain double precision
so editing one coordinate does not silently round an untouched coordinate.

The recipe Inspector, graph metadata, schema, preset parser and public terrain
page describe the same controls. The terrain preset example schema now separates
landforms from biome appearance and its asset was reimported through the CLI.

## Reproducible probe

`examples/rocky_valley_3d` is separate from the user's sculpted
`terrain_graph_3d` source. Seed 1704, a 2,000-by-2,000-unit extent and a
500-by-500-cell grid generate elongated warped ridges and a curved valley.
The corridor blends toward a low floor, followed by drainage, erosion and a
fixed-elevation river. This is not drainage-derived river routing. The README
records shape parameters and the native camera. All eight asset hashes and
references were checked; the native world contains 775 renderable entities.

## Validation and visual evidence

Release core/editor generation checks pass: terrain seed, preset, graph,
landform, graph-landform controls, patch, generator, graph settings, graph
history and graph document, plus structured-value, ImGui input and graph UI
(13 tests). Coverage includes default sample parity,
seed isolation, generation invalidation, invalid domains, path endpoints,
repeated points, cancellation and height blending.

CLI validation passes for `rocky_valley_3d` (11 files), `terrain_graph_3d`
(15 files) and `terrain_3d` (26 files). The new example completes a three-frame
headless smoke. A fresh Release Vulkan runtime at 2954-by-1744 was captured
alone after rebuilding, then stopped. The temporary capture is
`/tmp/rocky-valley-final.png`; it is visual evidence, not a benchmark artifact.
Earlier overlapping diagnostic captures are excluded from performance claims.
No new performance improvement is claimed for this generation step.

## Remaining gates

The native capture shows coherent valley/ridge placement, but also hard,
sawtooth material boundaries and grid-like shadow striping on steep surfaces.
Earlier visual setting comparisons showed that disabling shadows removes
the striping; ordinary CPU terrain also exhibits the hard material boundaries.
Moving world-position derivatives ahead of divergent fragment discards fixes
unsafe shared shader ordering, but the refreshed capture still shows striping.
Shadows remain enabled in the probe; this is an open renderer issue.

The example still uses solid material colors. Smooth layer blending, PBR rock
textures, production vegetation, drainage-derived rivers, basin/coastline/plateau
controls and the reference-scene visual acceptance remain incomplete. Milestones
3 and 7 remain open; this step does not provide CPU terrain/collision paging or
coordinated water/scatter residency. Desktop editor mouse interaction and Android
device rendering have not been newly qualified by this step. Public documentation
is updated in source; no website or package-store deployment was performed.
