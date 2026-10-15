# Terrain Milestone 4 qualification

Completed: 2026-10-15. Native qualification uses Linux Release, SDL/X11 and Vulkan.
The engine remains experimental lightweight 3D; this milestone supplies surface
materials, not dense cliff geometry, vegetation, atmospheric lighting or water.

## Shared authoring and classification

Terrain and meshes use ordinary Material assets. Albedo/emissive images use
hardware sRGB decoding before filtering; data maps remain linear and normal
mipmaps renormalize vectors. Live RenderTargets retain their existing encoded
presentation contract. Terrain height maps bias material weights without moving
vertices or changing collision. Shared controls include triplanar mapping,
normal convention/strength, detail normals, height-blend strength and macro
variation. The existing Surface Blending checkbox also controls mapped layers.

The surface classifier consumes signed sediment transport belonging to the
selected graph output. Serial erosion accumulates transport, blends combine it
using their actual weights, and unrelated graph branches do not contribute.
Positive deposition and exposed cut affect material roles. Biomes expose
`allow_snow`; default true preserves permissive authoring, while warm starter
presets opt out. Ground and rock eligibility overlap so finite slopes cannot
fall into an unassigned band. Invalid mask values remain diagnosable.

Generator version is 11, standalone cooked fields use version 4, and prepared
Terrain assets use payload version 9. Classification changes invalidate derived
caches. Rebuild cooked outputs; authored document format versions remain intact.

## Renderer ownership and residency

`TerrainGpuFieldData3D` resolves selected biome/role pairs and packs immutable
palette metadata. Explicit biome Material assignments remain authoritative.
Unused role bindings do not allocate array layers. There is no four-material
limit: four neighboring lattice samples are a local interpolation footprint.

`TerrainMaterialArraySource3D` snapshots canonical materials and image sources.
`TerrainMaterialArrayData3D` packs semantic images through the shared resize and
mipmap helpers. `TerrainMaterialArrayCache3D` owns asynchronous preparation,
transactional GPU publication and exact live allocation accounting.
`TerrainMaterialResidency3D` coordinates quality caps and camera demand. The
renderer consumes these owners rather than reimplementing them in the editor.

Five array slots carry albedo, normal, packed AO/roughness/metallic/height,
detail normal and emission. Globally unused slots are 1x1 arrays with the full
layer count, retaining scalar fallback values without full-resolution neutral
images. Real maps retain their selected edge. Opaque shadow casting uses categorical palette
coverage without sampling these shading maps. Mapped terrain needs twelve
sampler slots, texture arrays and sufficient layer capacity. Custom programs,
unsupported raster state, live RenderTarget maps, nearest or mip-disabled
sampling, or incompatible per-map wrapping use the explicit ordinary-material
fallback. That fallback retains discrete layer assignment.

Environment settings control maximum texture edge, GPU texture budget and detail
distance. Actual source dimensions, device limits and complete mip footprints
constrain tiers. Distance halves the requested edge, then quarters it beyond
four times the detail distance; detail normals fade independently. Camera demand
is combined across the current and preceding presentation. A ready tier remains
usable during replacement preparation; a strict budget can require temporary
ordinary rendering while outstanding GPU leases retire.

Array accounting covers live texture payload allocations, including borrowed
draw bindings. It excludes driver overhead, framebuffers and other renderer
resources; the material budget is not a limit on total process memory or VRAM.
CPU staging reserves packed output, one layer's scratch, the largest sequential
decoder workspace and unique encoded snapshots. It is not a hard process-RSS
limit: first-use source reads/hashes are still synchronous and precede worker
admission. Initial terrain generation, collision creation, shader compilation
and compatibility draws also contribute to startup cost.

2D overlay images now register lazily and resolve only when actually drawn.
Ordinary 3D map variants retire after their current/previous presentation demand
expires. Consequently mapped terrain does not keep unused HUD copies or its
initial ordinary fallback maps resident forever. Missing-source failures are
reported once until reload. Asset availability remains distinct from GPU image
residency.

The unused older global-four-role blend helper and nominal-size residency planner
were removed. Their dedicated tests were replaced by tests of the active owners;
they no longer offer a conflicting implementation or fabricated image sizes.

## Starter package

`polyhaven.materials.terrain` contains five CC0 1K source surfaces: Rock Face 03,
Grass Ground, Rocky Gravel, Sand 02 and Snow 02. Wet Gravel reuses gravel art with
a darker tint and scalar roughness; it omits the dry roughness map. Physical tile
widths and original artist/source/hash records accompany the package. Maps are
provider originals, not synthetic diagnostic patterns. Gentle macro variation
reduces repeated color patterns; it does not manufacture large cliff features.

The rocky-valley example uses this package through a material set. Geometry and
graph shape remain unchanged. Broad tiled cliffs can still reveal their source
pattern; these starter maps do not replace art direction or geometric detail.

## Native regression method

`scripts/terrain_material_pixel_probe.py` creates isolated red/blue mapped
fixtures. Capture the client image only after the material-residency and patch
counters become positive. Zero height strength must produce a linear-light
midpoint near `(188,0,188)`; contrasting height maps at strength one shift it
near `(255,0,2)`. Surface Blending off selects one triangle material.

A real Vulkan run caught a build-definition error: the directional mapped
fragment compiled without its mapped flag and read five-texel records using the
solid two-texel stride. A separate mapped shader source now makes the variant
explicit. Tests inspect actual compiled sampler metadata on Vulkan and compiled
GLSL declarations/uses on OpenGL and OpenGLES, in addition to interface linking.
Noop placeholder programs alone cannot qualify shader behavior.

Source and cooked height-probe captures are pixel-identical. Temporary evidence:
`/tmp/m4-final-height.png`, `/tmp/m4-final-neutral.png`,
`/tmp/m4-final-cooked.png`. Center pixels are `(255,0,2)` and `(188,0,187)`;
the one-level neutral asymmetry comes from pixel-center placement.

## Final checks

Twenty-eight focused Release suites pass, including source/schema contracts,
material parsing and import, compiled shader variants, shared GPU resources,
2D lazy resolution and drawing, 3D map retirement, array packing/residency,
terrain classification/cooking, and editor controls/history/workspace behavior.
All 32 repository examples validate. The cooked rocky valley validates 43 files.
This is a focused gate, not a claim that every repository test was run.

Final native captures use one runtime at a time, with no build running during
measurement. The height and neutral fixtures produce the expected pixels;
sharp mode contains exactly two colors. The complete rocky-valley source and
cooked captures are also pixel-identical at 3840 x 2022:
`/tmp/m4-final-valley.png` and `/tmp/m4-final-valley-cooked.png`.
`/tmp/m4-final-close.png` checks surface scale on steep faces;
`/tmp/m4-final-dusk-fast.png` checks alternate warm directional lighting.
Terrain layering remains readable without stretched top-down cliff projection.
Coarse terrain geometry and grazing-shadow artifacts remain visible at close
range; these surface materials do not conceal those separate limitations.

The native Material Editor was inspected at 5120 x 2806. Its shared controls
show normal convention, projection, height blend, macro variation and map IDs.
The screenshot exposed an obsolete height tooltip, corrected before the final
editor rebuild. The existing narrow Inspector layout wraps labels; a shaded
asset preview and broader layout refinement remain separate editor work.

### Measured workload

Rocky Valley, Linux Release/Vulkan, 3840 x 2022, 4x MSAA, default 128 MiB
material-array budget and 1K quality cap. Excluding the first 200 of 1000 frames:

| Scope | Median | p95 |
|---|---:|---:|
| GPU frame | 10.965 ms | 12.411 ms |
| Update | 1.735 ms | 2.240 ms |
| CPU render preparation | 2.327 ms | 2.947 ms |
| Frame total including presentation | 10.890 ms | 12.253 ms |

These overlapping pipeline measurements must not be added together. Presentation,
OS load and hardware affect comparisons; no general FPS guarantee is implied.
Evidence: `/tmp/m4-final-valley.frames.csv` and its `.profile.csv` companion.

The ready field submits 189 patches in four main-view draws. Material arrays
hold 117,440,540 bytes (about 112 MiB), compared with 195,734,140 bytes (about
187 MiB) before neutral-slot compaction: roughly 40 percent less payload storage
with the same real-map resolution. Pending staging bytes settle to zero.
The final probe fits the default budget without an example-specific override.
Initial source reads, collision creation, shader compilation and compatibility
uploads still cause startup work; the warm figures deliberately exclude it.

Vulkan native rendering is qualified here. OpenGL/ESSL shader variants compile
and their sampler contracts are checked, but Windows and physical Android native
rendering have not been qualified by these runs.

## Publication and deployment

`polyhaven.materials.terrain@1.0.0` is published at
https://demiengine.de/packages/polyhaven.materials.terrain. A fresh installation
from the public registry using an empty package cache validates 35 files with no
diagnostics. The repository example retains its relative local-registry path so
source-checkout development remains reproducible.

The store release `20261015-terrain-m4-final` passed Composer tests, validation of
all 106 Twig templates and production cache warmup before atomic activation.
The live health endpoint and updated Materials/Terrain pages return successfully.

Publishing the larger art package exposed an existing Nginx configuration bug:
the internal authentication subrequest inherited the 1 MiB general body limit
and rejected the upload's Content-Length with 413, surfaced as HTTP 500. The
internal location now matches the upload route's 600 MiB limit, while remaining
internal and forwarding no body to authentication. Nginx configuration validation
passed; an unauthenticated 2 MiB upload still returns 401, and the authenticated
art upload succeeds. The maintained deployment template and operations notes
include the correction.
