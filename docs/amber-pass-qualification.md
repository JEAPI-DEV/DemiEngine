# Amber Pass mounted demo qualification

## Scope

`examples/amber_pass_3d` is a playable Linux 3D example with native editable
Terrain, a mounted capsule controller, in-place skeletal horse clips, a
collision-aware orbit camera, original rider/chapel geometry and CC0 scenery.
The mountain backdrop is a continuous 720 × 720 metre native terrain field,
with ridged noise and a path-distance mask protecting the road. It no longer
uses enlarged open cliff-scan meshes. Tileable gray Marble Cliff 05 maps shade
mountain slopes. Props sample the same native grid triangles inside the
protected corridor; mountain placement must use native terrain queries.

There are 80 trees and 30,637 grass clumps after road filtering, with
independent view culling and real full/medium/low grass geometry. Geometry
counts and provenance are in `assets/environment/summary.json` and
`downloads.json`. Source art remains editable in Blender. The horse is also
available as the local `demi.animals.horse@0.1.0` package. This does not establish
online publication or Android qualification.

The rider is a rigid seated mesh. The fast horse clip is a running trot; rider
IK, combat, volumetric atmosphere and large-world streaming are not implemented.
Environment fog fields are not relied on for this composition.

## Shared defects found through the demo

- Native resource upload replaced the entire renderer registry per asset.
  Ready uploads now publish in a batch. Release, cancellation and shutdown also
  remove a batch at once; failed publication preserves tracked residency.
- Scene preparation resolved and materialized the same scene twice. It now
  reuses the already resolved asset IDs for its implicit resource group.
- Demo foliage placed `alpha_cutoff` at the material root, where it was ignored.
  It now uses `render_state.alpha_cutoff`; the shared material parser rejects
  misplaced render-state fields with a diagnostic.
- PBR surfaces sampled normal/scalar maps before rejecting transparent pixels.
  They now capture derivatives, evaluate coverage, then use explicit-gradient
  samples for surviving pixels. Shadow texture taps use their existing mip 0
  explicitly. Geometry, material maps and shadow coverage remain available.

Upload time is a scheduling target between indivisible backend transactions,
not a hard cap on an individual registry replacement. Initial native texture
preparation is still visible; this change does not claim asynchronous loading
of every native renderer resource.

## Measurements, 2026-10-18

Release, SDL3/X11, Vulkan, RTX 3070 Ti Laptop GPU. Only one native editor/runtime
was running. No build or Blender render ran during measurements. The laptop was
plugged in for the comparisons; observed graphics/memory clocks were
1200/5486 MHz and power about 80 W. Earlier battery runs reported a 20 W ceiling,
210/375 MHz clocks and are not comparable hardware-performance samples.

The controlled shader comparison used identical camera/assets/settings at
1920 × 1080. Frames 180–599 exclude startup and wait for terrain GPU resources.
These are native profiler medians in milliseconds:

| Scope | Before early cutout | After early cutout |
| --- | ---: | ---: |
| Frame total | 10.702 | 10.556 |
| GPU | 11.160 | 10.970 |
| Main GPU view | 5.496 | 5.303 |
| Update | 0.569 | 0.540 |

The shader improvement is modest. The much larger battery-to-AC difference
must not be attributed to it. Raw traces were captured as
`/tmp/amber-cutout-before.csv` and `/tmp/amber-cutout-after.csv`.

The final scene then received revised grass silhouettes, lighting and terrain
art. At primary-desktop content resolution **3840 × 2022**, frames 180–599 gave:

| Scope | Median ms | p95 ms |
| --- | ---: | ---: |
| Frame total | 13.285 | 15.170 |
| GPU | 13.431 | 15.050 |
| Main GPU view | 8.495 | 10.176 |
| Update | 0.696 | 1.020 |

The fixed-camera view submitted about 2.16 million triangles, including shadow
passes; terrain patch fallbacks were zero in the measured interval. Frame total
and GPU time are overlapping asynchronous measurements, not quantities to add.
This qualifies the starting view, not all camera routes or all hardware.
Trace: `/tmp/amber-final-desktop.csv`; screenshot:
`/tmp/amber-final-desktop.jpg`. Captures wait for terrain material residency;
early frames use the discrete fallback and are not final material evidence.

The final bounded 600-frame run exited in 27.17 seconds, including startup and
shutdown. Earlier runs spent roughly another minute tearing down resources;
shared batched release removes that repeated renderer reconstruction.

## Checks

- Release build of native runtime/editor and affected focused test targets.
- Asset streaming, runtime asset service, canonical Material and compiled
  material shader link tests passed. Shader checks inspect optimized GLSL/ESSL
  and link the generated mesh/terrain/water interfaces, including SPIR-V.
- Amber Pass validation: 131 files (after removing the unused prototype vegetation dependency), no diagnostics. Rocky Valley and Vegetation
  example validation also passed.
- Native riding E2E passed movement, grounding, braking and camera placement.
- The mounted prefab opened and rendered in the native editor at 3840 × 2022.
  This exposed and fixed two idle handoffs: deferred dock focus and target
  preparation after a view first publishes its size. Docking/target lifecycle
  tests cover hidden panels, modal blocking, resize, close, reopen and failed
  allocation without a redraw spin. Screenshot: `/tmp/amber-prefab-verified.jpg`.
  Its initial framing clips the helmet; that nonblocking camera-framing issue
  is recorded in `tofix.md`.
- Linux cook completed; the cooked project passed a six-frame headless smoke.
  No editable Blender source entered the cooked output.
- Lua script checks passed. Horse package source test has no Lua tests;
  package installation and referenced assets were validated through the demo.

This is a focused qualification, not a full repository test-suite result.
