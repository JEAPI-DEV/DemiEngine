# Terrain GPU patch qualification, 2026-10-12

Final focused Release gate: 21 CTest suites passed, covering backend texture and
instance commands, renderer/shadow/visibility behavior, patch topology and
batching, field packing/cache lifecycle, CPU residency, component/schema
contracts, terrain publication and editor history/workspace behavior. The
`terrain_graph_3d` and `terrain_3d` examples validated without diagnostics.
This is not a full repository or physical Android qualification gate.

## Delivered behavior

Compatible terrain fields now render through shared indexed patches, instancing,
R32F heights, canonical RGBA32F normals and per-pixel biome/role material palettes.
Full perimeter samples preserve mixed-level chunk boundaries. The main and
shadow passes share displacement and material culling. Full CPU collision,
heightfield queries and picking remain independent of this render path.

Material boundaries intentionally use per-pixel map sampling instead of the
previous triangle-majority partition. This accepted alpha/beta visual change
preserves authored material bindings and biome overrides, but does not promise
bit-identical images. Custom programs, textures, cutout/transparency, unsupported
raster/depth states or device capabilities retain ordinary mesh rendering;
they are not silently replaced with a palette material.

The native probe initially fell back because assigned materials used clockwise
culling while unassigned defaults were two-sided. Palette entries now carry a
CW-cull flag; a mixed field rasterizes both sides and discards the applicable
front-facing fragments. Pinned bgfx defines clockwise fronts, so the predicate
is the flag combined with `gl_FrontFacing`. It applies before main/shadow
branching. Homogeneous fields can retain fixed raster culling.

## Workload and method

Both paths used the same newly launched `linux-release` binary, SDL3/bgfx Vulkan,
2954x1744 window, existing 500x500-cell / 2000x2000-unit landscape, current
material palette, 5000-unit far clip, four shadow cascades and 4x MSAA. Source
resolution, camera sample sequence and lighting were not reduced. The benchmark
runs 600 frames, excludes 60 warmup frames and disables VSync. Each table below
uses the `full` run: pixel-error budget zero. This tests submission reduction
without attributing it to coarser geometry.

The mesh control sets `terrain_gpu_cache_mb` to zero in a temporary project
copy; the patch run uses automatic selection. The benchmark also records a
2-pixel run, but the comparison below uses identical Full-detail settings.
The authored example, including its manual edits, is preserved.

Binary SHA-256 for the initial and repeat comparisons:
`712d9a5e6217c2ba05f10e8ec985b2aa7c2d0e8bc9287b1a62473f919c8c6a67`.
Source scene SHA-256:
`d1342be5ba27b3d5b1b4887f9552f231d2392de1dc9fd8bbd8eae9e1b968aab2`.

Reproduction from the repository root:

```sh
python3 scripts/benchmark_terrain_lod.py --binary build/linux-release/demi --build-configuration linux-release --terrain-renderer mesh --width 2954 --height 1744 --frames 600 --output /tmp/terrain-gpu-final-mesh
python3 scripts/benchmark_terrain_lod.py --binary build/linux-release/demi --build-configuration linux-release --terrain-renderer auto --width 2954 --height 1744 --frames 600 --output /tmp/terrain-gpu-final-patches
```

Session evidence is in those directories' `summary.json`, `full-frames.csv`,
`full-profile.csv` and `full.log`. Repeat reports use
`/tmp/terrain-gpu-repeat-mesh/` and `/tmp/terrain-gpu-repeat-patches/`.
Temporary reports are not durable repository artifacts; this record retains
configuration, hashes, method and measured results.

## Results

Warm medians, with frame/CPU/GPU times in milliseconds:

| Metric | Mesh control | GPU patches |
| --- | ---: | ---: |
| Frame total | 9.492 | 3.816 |
| Frame total p95 | 11.059 | 6.229 |
| CPU render preparation | 7.009 | 2.734 |
| CPU render preparation p95 | 8.081 | 3.346 |
| GPU time | 1.201 | 1.086 |
| GPU time p95 | 2.052 | 2.078 |
| Main-view draw batches | 1,115 | 5 |
| Shadow draw batches | 635.5 | 4 |
| Main-view triangles | 183,820 | 187,135 |

The patch path uses one terrain batch in the main view and four terrain shadow
batches, with other scene geometry accounting for the remaining main batches.
The fractional shadow median comes from the two central observations of 540
integer frame counts. Warm patch fallback counts were zero.

CPU render preparation falls about 61%, and total measured frame work about
60%. The GPU median improves modestly; its p95 does not improve in this pair.
Triangles increase about 1.8% because whole-patch bounds replace finer per-biome
culling. This is submission reduction, not lower source density or fewer
rendered triangles.

An independent repeat with the same binary and source hashes produced:

| Metric | Repeated mesh control | Repeated GPU patches |
| --- | ---: | ---: |
| Frame total median | 9.256 | 3.730 |
| Frame total p95 | 10.849 | 6.043 |
| CPU render preparation median | 6.945 | 2.583 |
| CPU render preparation p95 | 7.908 | 3.275 |
| GPU median | 1.166 | 1.088 |
| GPU p95 | 1.939 | 2.697 |

Both repeats retain 1,115 versus 5 main batches and 635.5 versus 4 shadow
batches. The CPU result repeats; GPU-tail variance prevents a stronger GPU
latency claim.

Physics code and Full collision did not change between these runs. Faster
render cadence means fewer fixed steps per rendered frame: warm records with
`Physics3D.step` fell from 307 to 145 in the initial pair. Their timing medians
were 1.298 and 1.353 ms. The lower per-frame update cost must not be reported as
a physics optimization.

Warm field textures occupy 6,025,080 bytes and shared patch topology 37,644
bytes. The mesh control's terrain GPU mesh payload median was 26,438,820 bytes.
These counters describe particular payloads, not all device allocations or
process memory. Process RSS medians were about 500.8 MB versus 505.6 MB; Full
CPU terrain geometry, source data and collision remain resident. Warm pending
field jobs/upload bytes were zero, so preparation was complete in the measured
window; startup allocation/preparation spikes were excluded, not eliminated.

## Native visual check and limits

The session capture `/tmp/terrain-gpu-native.png` is a 1600x945 native visual
check, separate from the timed benchmark window. It shows the lake, rock/snow
surfaces, green shoreline regions and surrounding terrain using the patch path.
It is not a photorealism qualification or evidence of improved landform
composition, forestry, PBR textures or smooth surface transitions.

`terrain_gpu_cache_mb` defaults to 128 MiB and covers field/patch GPU payload.
A zero GPU budget, insufficient admission budgets or unsupported capabilities/
materials select mesh fallback. Zero preparation jobs/staging or unavailable
workers prevent new packing; already ready, material-valid bundles remain
usable. CPU preparation is asynchronous,
admitted against staging/job budgets; ready bundles are immutable, camera
readiness is frozen per presentation and four-texture replacement is
transactional. Material changes can reuse unchanged height/normal channels and
identical classification bytes.

Changed channels are replaced as whole textures. Dirty-region uploads and
texture paging are not delivered. Neither are CPU collision paging/admission,
terrain PBR texture layers, vegetation, coordinated water residency or a
photorealistic landscape preset. Desktop Vulkan evidence does not qualify
physical Android or Windows/Direct3D; native Direct3D selection and Windows
shader backend integration remain missing.
