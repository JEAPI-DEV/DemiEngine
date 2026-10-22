# Demi Woodland Environment

Reusable CC0 photographic woodland assets from Poly Haven, converted for the
Demi bgfx renderer. See CREDITS.txt for source links and derivation details.

Public prefabs: prefab://demi/woodland/tree, grass, rock and rock_tall.
All prefabs use ordinary nested entities and MeshRenderer LOD fields, without
Lua or collision bodies. Tree geometry has separate textured trunk, branch and
alpha-tested leaf-card parts; foliage is never replaced with solid primitives.

Trees and rocks have no distance cull (camera visibility/far clipping still
applies). Medium geometry starts at 50 m; the low tier at 150 m currently reuses
the authored medium mesh, preserving silhouette and UV coverage. Trees use
19997 triangles nearby and 6000 at distance. Grass uses 1008/237/8 triangles,
changes tiers at 35/90 m and culls at 220 m. Grass source tufts are 0.16 m tall;
placement scale controls the desired coverage and grass height. Terrain instance
batching preserves these standard prefab LOD and Material contracts.

This is an environment-art package, not a renderer feature or a performance
claim. Counts, visible texture quality and frame times require scene-specific
qualification. No editorial/source Blender files are shipped.

## Layered understory (introduced in 1.1.0, current 1.2.0)

New public prefabs use original photographed Poly Haven plants with green
source color and real alpha, without the older autumn exposure treatment:

| Prefab suffix | Height at scale 1 | Near / medium / low triangles |
| --- | --- | --- |
| `grass_lush` | 0.65 m | 6422 / 4626 / 10 |
| `shrub` | 0.9 m | 2385 / 2134 / 1790 |
| `sorrel` | 0.18 m | 990 / 414 / 353 |
| `wildflowers` | 0.4 m | 3540 / 2728 / 752 |

Prefix each with `prefab://demi/woodland/`. These scale-one sizes are already
in engine metres; do not reuse the old grass prefab's 3–5x corrective scale.
Shrub is a vertical leafy understory plant. Sorrel is low broad-leaf cover with
small pink flowers; Gazania supplies the larger orange wildflower clump.

Near and medium plants retain complete original source mesh geometry. The far
lush grass uses four crossed views plus one top coverage quad baked from the
actual near plant (10 triangles total). Other far plants use complete
lower-complexity source variants. LODs are selected at
35/90 m and cull at 220 m. These costs are intentionally higher than the old
small tuft: combine density/visibility qualification with close visual review.
The package does not claim that 120,000 full-detail plants fit every GPU.

Each species uses original 1K RGB photographs merged with the official alpha,
original normal maps and byte-preserving separated AO/roughness. No new solid
leaf primitives or synthetic texture artwork is substituted. UV0 and authored
normal data remain in the exported source GLBs. See `sources-understory.json`
for official download URLs and MD5 records, and `understory-summary.json` for
actual exported bounds, triangles and file sizes.

To reproduce, download the checked sources into one folder per asset matching
`/tmp/demi-understory/<source-id>/`. Run headless Blender with
`--python tools/build_understory.py -- /tmp/demi-understory`, then run
`python tools/prepare_understory.py /tmp/demi-understory` with Pillow installed.
The source-record file records paths relative to each source folder. Keep the
original downloaded API response as `files.json` there for texture preparation.

## Authored grass coverage LOD in 1.2.0

The previous far grass had 940 triangles per plant. Its replacement has 10:
four crossed vertical cards from transparent orthographic views of the full
6422-triangle near mesh, plus a top-view ground-cover quad for aerial cameras.
This is an offline artwork LOD for this particular grass patch, not a general
runtime impostor feature. Near/medium vertex geometry remains intact.

All grass LODs share the ordinary Material contract. Original 1K image pixels
occupy the upper-left quarter of a 2K atlas, four side views occupy upper-right,
and the top view occupies lower-left. Near/medium UVs select the original image
quarter; card UVs select their baked views. Source colors use unlit emission,
Standard sRGB and real transparent alpha; the bake does not include scene light
or cast shadows. Original normals/AO/roughness remain in their original region;
far cards use flat/neutral map values and ground-facing vertex normals.

The side and aerial comparison renders in previews/ show the original near mesh
on the left and the actual authored coverage LOD on the right. Both were rendered
with the same unlit atlas Material. Their ordinary MeshRenderer material is lit
at runtime. Five view images and grass-impostor-bake.json record the actual bake
angles, alpha coverage and crop bounds. Every view is nonblank; the extra top
view keeps aerial coverage instead of exposing edge-on empty vertical planes.

To regenerate 1.2.0, first run the two 1.1 source preparation steps above, then
run headless Blender with `--python tools/bake_grass_lod.py`, followed by
`python tools/compose_grass_atlas.py`. For the comparison images, run headless
Blender with `--python tools/preview_grass_lod.py`. Regenerate source meshes/maps
before rebaking: the scripts deliberately reject an already-composed atlas.
