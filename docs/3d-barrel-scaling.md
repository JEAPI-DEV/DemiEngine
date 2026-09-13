# Imported barrel scaling — 2026-09-13

## Asset and collision

The performance lab now supports `geometry = "barrel"` and both benchmark
runners accept `--geometry barrel`. Primitive benchmark defaults remain intact;
the editable interactive scene opens with 250 falling barrels.

The original barrel was authored through Blender MCP in a separate scene,
preserving the user's original scene. The editable source is
`examples/performance_3d_lab/assets/models/barrel/barrel.blend`; the GLB and normal
Model3D import manifest sit beside it. It has rolled rims, two reinforcing ribs,
inset end caps, and two fill caps: one static mesh, two source materials,
2,676 triangles, no textures or animation. Model inspection confirms normalized
Y-up dimensions of 0.70 × 0.95 × 0.70 m, centered at the origin. The triangulated
export supplies normals, UVs, and tangents without inspection diagnostics.

The reusable `prefabs/barrel.prefab.json` owns a 32-point, 16-sided convex
cylinder matching those outer dimensions. It approximates the outer envelope,
not the small rib recesses or fill-cap detail. It is not a sphere or box proxy.
Runtime prefab overrides supply the same body motion/damping/reporting settings
as the lab. Mesh-only barrel runs omit physics entirely, as primitive mesh mode
does. No engine, solver, or rendering code changed for this test.

Both a Blender preview and a close-up running-engine view were inspected. The
engine view was captured through X11 solely for visual review; performance runs
used the normal Wayland launch path. Blender's preview lighting/material response
is not a claim about engine PBR fidelity. No custom shaders or image textures
were introduced.

## Measurement conditions

Blender remained open after authoring, with no Blender render jobs during the
sweep. Release binary, hybrid mode, NVIDIA then Radeon, 2,000 and 5,000 instances,
moving and pile workloads, VSync requested on, three repetitions each. Runs use
12 seconds of game-timer duration and exclude the first two wall seconds of
trace timing. Actual pixels are 1920×1080 on the 165 Hz laptop panel; the logical
request is 1440×810 because of 133% display scaling.

All 24 runs passed capture checks: intended GPU IDs, correct resolution/refresh,
no focus loss, minimization, or interruption, GPU timing available, zero native
physics update errors, and every barrel plus the floor visible with zero meshes
culled. They render in **two batches**, confirming imported static-model
instancing. The complete 2,676-triangle mesh is used at every distance, without
LOD. At 5,000 instances this submits 13.38 million barrel triangles before the
floor. Capture validity does not imply performance qualification.

Spawn positions, spacing, camera, and fixed-step/catch-up settings remain the
same as the primitive lab. Barrel piles start with small deterministic tilts
(at most 0.1 radians around X/Z) to exercise tipping and rolling. No ongoing
nudges, artificial settling, or positional corrections are added. Their greater
height, flat ends, convex collision cost, and initial tilt make this a different
physics workload from spheres, not a controlled single-variable comparison.

## Results

Medians of three per-run measurements, after warmup. FPS is frames / wall time;
p95 is milliseconds. Physics/frame can include two fixed steps. Simulation %
is physics time advanced / scaled input time; short-window accumulator carry
can produce values fractionally above 100% without implying faster simulation.

| GPU | Barrels | Workload | Average FPS | Frame p95 | Physics/frame p95 | GPU p95 | Simulation % | Discarded fixed time (ms) |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| NVIDIA | 2,000 | Moving | 159.9 | 10.54 | 5.42 | 3.10 | 100.0 | 0 |
| NVIDIA | 2,000 | Pile | 158.8 | 11.22 | 5.92 | 2.01 | 100.0 | 0 |
| NVIDIA | 5,000 | Moving | 20.9 | 55.03 | 32.60 | 12.93 | 69.6 | 3,133 |
| NVIDIA | 5,000 | Pile | 12.5 | 91.20 | 68.66 | 16.29 | 41.7 | 6,017 |
| Radeon | 2,000 | Moving | 160.8 | 10.65 | 5.66 | 4.48 | 100.0 | 0 |
| Radeon | 2,000 | Pile | 161.2 | 11.26 | 6.27 | 4.43 | 100.0 | 0 |
| Radeon | 5,000 | Moving | 21.5 | 51.84 | 32.44 | 8.35 | 71.6 | 2,933 |
| Radeon | 5,000 | Pile | 12.7 | 89.40 | 67.87 | 8.54 | 42.4 | 5,967 |

All 2,000-barrel runs discarded zero fixed time; all 5,000-barrel runs discarded
some. The 5,000-barrel pile also incurred occasional wall-delta clamping (up to
13.61 ms accumulated per measured run). Startup clamping/loading is separately
recorded and excluded from the timing comparison: this is not a startup-latency
qualification. Worst per-run p99 at 2,000 was 13.12 ms; at 5,000 it reached
99.74 ms. Average FPS alone would hide both uneven pacing and simulation slowdown.

At 2,000, these barrel workloads keep up comfortably in this short test. At
5,000, both physics/CPU costs and full-detail model rendering are substantially
heavier; they do not qualify as stable real-time gameplay. GPU cost is no longer
negligible, although the pile's CPU physics cost remains much larger. GPU clocks,
thermals, and background load were not isolated: the cross-GPU timing differences
are not a general hardware ranking. Nor does this establish support for thousands
of animated, textured, gameplay-heavy characters.

## Validation and use

- Model inspection and project validation pass without diagnostics.
- Mesh, rigid, and pile barrel variants passed small headless startup smokes.
- The real desktop E2E test passed: instantiate/release the prefab, let it fall
  upright and sideways, verify resting heights, hit its center with a ray, and
  miss the bounding-box corner outside the round hull.
- Five timing-summary unit tests pass. Source whitespace checks pass.
- Early headless E2E attempts did not complete the waits under the launcher's
  short default frame limit; the passing E2E result is the visible desktop run.

Open `examples/performance_3d_lab/scenes/main.scene.json` to change the interactive
population. The art and collider remain separately editable in Blender and the
prefab. The model README describes export/reimport and collision-update steps.

Raw evidence is in `build/barrel-scaling-nvidia/` and
`build/barrel-scaling-radeon/`, including per-run geometry labels and binary
hashes. Reproduce the hybrid wrapper from
[the earlier GPU report](3d-hybrid-graphics.md#evidence-and-reproduction) with
fresh output names and `--geometry barrel --counts 2000 5000 --workloads rigid pile
--vsync on --seconds 12 --warmup-seconds 2 --repeats 3`. Keep the actual 1080p
dimension check; do not confuse the logical window request with render pixels.

Future performance comparisons should explicitly separate full-detail rendering
from model LOD and convex physics from simpler proxies, while retaining collision
correctness. This task adds an asset/workload probe, not an engine optimization.
