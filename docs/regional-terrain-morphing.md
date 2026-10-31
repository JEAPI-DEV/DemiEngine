# Regional terrain morphing and GPU accounting qualification

## Scope and ownership

2026-10-31, Linux Release. This follows the
[range/startup qualification](regional-terrain-range.md).

`RegionalTerrainCoverSurface` samples the actual rendered cover's piecewise
triangles, including stitched edge heights. Bilinear interpolation or resampling
the procedural graph would not reproduce the old mesh and would introduce a
jump at transition start. The helper retains only the region extent and
immutable field snapshots, not a borrowed recipe or live-world pointers.

`RegionalTerrainMorph` owns one 0.25-second smoothstep clock for all changed
patches and stitched neighbors. Refinement publishes the finer grid on the old
surface and moves it to the new surface. Coarsening retains the old fine grid,
moves it to the new coarse surface, then publishes the coarser topology. Other
cover changes wait until this coordinated transition finishes. Camera demand is
re-evaluated afterward. Very fast traversal can therefore outrun refinement;
this is not a GPU clipmap implementation or unlimited streaming throughput.

The world keeps a complete cover throughout. Morphing is render-only: immutable
collision arrays are not rewritten each frame. Their existing replacement still
occurs at fixed-step publication. Regional GPU terrain interpolates the same
endpoints used by the native fallback, including while GPU resources prepare.
Culling bounds contain both endpoint surfaces. Shading normals, biome appearance
and vegetation model LODs are not themselves cross-faded by this change.

The transient source heights live in `HeightField::morphFromHeights`, outside
authored and cooked data. Ordinary fields retain their R32F height texture;
transition fields use RGBA32F to pack final/from heights without another sampler.
The existing instance record's fourth control channel carries the common blend
amount to camera and shadow passes. Finished transition fields remain immutable
until normal eviction, avoiding a second upload just to discard their start
heights. No public component field, Lua alias or compatibility path was added.

Initial readiness now begins when the first complete, worker-planned cover is
published. Waiting for camera demand to become stationary could otherwise keep
loading visible and suppress all morphing while the camera moved. A regression
keeps moving the camera during bootstrap and checks this boundary directly.

## Performance defect

`TerrainGpuFieldCache3D::residentBytes()` used to lock and sum every live texture
weak pointer. Patch submission called it for each visible patch in each camera
and shadow view, turning a byte-count query into quadratic work.

Accounting now follows actual GPU texture ownership. Successful allocations add
their bytes once, and final destruction subtracts them once. Borrowed textures
remain charged after cache eviction; shared channels are counted once. Partial
upload failure, material invalidation, channel reuse and borrowed lifetime tests
continue to pass. This changes accounting cost, not scene quality or residency
limits.

## Matched maximized measurement

NVIDIA RTX 3070 Ti Laptop GPU, driver 615.71.09, Vulkan, Release. Both 100-second
captures used the same Alpine camera, vegetation, impostors, isolated editor
settings and maximized 5120 x 2742 window (2732 x 1710 terrain viewport). The final
600 frames of each capture provide these steady-state values. No concurrent
builds or tests ran during those captures.

| Scope | Before median / p95 | Accounting fix median / p95 |
| --- | ---: | ---: |
| Frame interval | 31.049 / 32.229 ms | 19.729 / 20.994 ms |
| Editor CPU frame | 30.614 / 31.865 ms | 19.313 / 20.533 ms |
| Viewport CPU | 28.361 / 29.455 ms | 17.088 / 18.142 ms |
| Directional-shadow CPU | 10.799 / 11.697 ms | 5.934 / 7.384 ms |
| GPU | 11.903 / 12.162 ms | 15.878 / 16.528 ms |

This isolates the accounting correction before enabling moving-camera morphs.
The observed GPU timing increased while CPU/frame time fell; it is reported as
measured, not attributed to an unverified GPU cause. These are single repeated
workload captures, not a hardware-independent FPS guarantee or a 60 Hz gate.
Raw captures: `/tmp/demi-morph-before.csv` and
`/tmp/demi-morph-accounting.csv`.

## Unattended rendered traversal

The terrain example's E2E route now drives its actual camera out and back for
2 km before distant teleports/revisits. It waits for initial collision coverage
and checks coverage again after the return. It needs no desktop mouse/keyboard
injection or renewed GNOME remote-control session.

The final visible native run passed all four scenarios and recorded 568 frames
with active regional morphs. All those frames have GPU timing samples; terrain
GPU patch and shadow-patch draws remained active. There were 628 frame records
with morph CPU work (including construction/completion frames): median 0.897 ms,
p95 2.594 ms, maximum 3.524 ms. These are whole-frame morph-scope totals, not
per-vertex costs, and include the actual changing-cover workload. They should
not be added to unrelated benchmark percentiles.

Raw run: `/tmp/demi-morph-traversal.csv` and
`/tmp/demi-morph-traversal.log`. The earlier synthetic desktop-input attempt did
not move the camera and was discarded as traversal evidence. An initial Lua
fixture error used a named vector field instead of `point[2]`; the corrected
native run above passed. The previous headless tour-transition issue was not
requalified by these visible tests.

## Verification

- All 80 selected terrain/editor/render CTest checks pass after rebuilding their
  relevant Release targets; this is not a full repository test run.
- New tests cover triangle-vs-bilinear interpolation, split/merge endpoints,
  intermediate seam agreement, CPU fallback and packed GPU height agreement,
  shared camera/shadow blend factors, unchanged collision data, moving bootstrap
  readiness and streaming in both directions.
- GPU cache tests cover morph-endpoint texture invalidation and exact accounting
  while old/new fields borrow shared textures after eviction.
- All four visible terrain-example E2E scenarios pass, with morph activity
  verified in the profiler rather than inferred from a successful launcher.
- `demi validate examples/terrain_3d` checks 203 files without diagnostics.
- `git diff --check` is clean. Existing unrelated worktree changes are retained.

```sh
cmake --build --preset linux-release --target demi-editor demi
ctest --test-dir build/linux-release --output-on-failure \
  -R 'terrain|editor.*hierarchy|editor-scene-view-state|editor-asset-workflow|bgfx-renderer3d|scene-visibility3d|bgfx-gpu-resources|bgfx-graphics-device'
./build/linux-release/demi test linux --project examples/terrain_3d \
  --timeout 300 --profile-frames /tmp/terrain-traversal.csv
```
