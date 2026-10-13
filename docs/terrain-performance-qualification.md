# Terrain frame-time qualification, 2026-10-11

## Workload and method

The branching terrain example uses a 500 x 500-cell heightfield, 1,229
renderables, automatic material sections, four directional shadow cascades and
4x MSAA. No terrain density, shadow setting or resolution was reduced.

The native Vulkan runtime loaded `/tmp/demi-terrain-material-cooked` with the
same scene camera. Requested window size was 2953 x 1743, matching the editor's
rendered Game region; backend reporting rounded the backbuffer to 2954 x 1744.
The editor itself used the external display's 5120 x 2806 client area.

Reproduction from the repository root:

```sh
./build/linux-debug/demi cook --project examples/terrain_graph_3d --platform linux --output /tmp/demi-terrain-material-cooked
SDL_VIDEODRIVER=x11 ./build/linux-debug/demi run --project /tmp/demi-terrain-material-cooked/demi.project.json --window-size 2953x1743 --max-frames 150 --profile-report /tmp/terrain-debug.csv --profile-frames /tmp/terrain-debug-frames.csv
SDL_VIDEODRIVER=x11 ./build/linux-release/demi run --project /tmp/demi-terrain-material-cooked/demi.project.json --window-size 2953x1743 --max-frames 300 --profile-report /tmp/terrain-release.csv --profile-frames /tmp/terrain-release-frames.csv
```

The table excludes the first ten frames, including initial collision creation
and GPU uploads. It uses per-frame CSV totals, not a mean distorted by startup.
Baseline and final comparison runs had no overlapping builds. Intermediate
experiments were diagnostic only. Forty debugger stack samples further located
component lookup and uniform-submission costs; debugger timings are not used
for the comparison.

## Results

| Native build | Median frame | p95 frame | Median update | Median render preparation |
| --- | ---: | ---: | ---: | ---: |
| Debug before fixes | 127.608 ms | 132.029 ms | 58.903 ms | 64.743 ms |
| Debug after fixes | 79.789 ms | 82.326 ms | 39.525 ms | 39.178 ms |
| Release after fixes | 7.284 ms | 9.509 ms | 0.848 ms | 5.802 ms |

The Debug preset uses `-g` without compiler optimization. Release uses
`-O3 -DNDEBUG`. The Release improvement must not be attributed entirely to the
code changes. Both builds rendered 73,130 visible triangles and 383 main-view
batches with four shadow cascades. Shadow batches fell from 806 to 706.

Slow Debug frames hit the existing four-step physics catch-up budget. In the
optimized run, ordinary frames frequently needed zero or one fixed step. Median
GPU timing was 14.909 ms in the original Debug run and 1.405 ms in Release;
the observed bottleneck was CPU preparation/synchronization, not a requirement
to lower scene quality on this hardware.

At startup, native Debug spent about 6.5 seconds in its first update constructing
collision shapes. The optimized run's maximum update was 372 ms. Warm-frame
results do not hide or count that one-time work as a recurring simulation cost.

## Changes retained

- Lighting collection resolves transforms only for light entities. One collected
  scene-lighting snapshot is shared across a frame's shadow and main views.
  The measured collection cost dropped from approximately 24 ms/frame to 1.4 ms
  in Debug. Authored environment/sky settings and editor lighting overrides are
  still separate inputs.
- Physics shape signatures and body placement share one resolved transform per
  entity per step. Shape mutation checks and deterministic stepping remain active.
- A direct child of a root uses the shared transform composition helper without
  allocating a cycle-detection set. Deeper hierarchies retain cycle validation.
- Complete opaque material-section groups with matching raster/depth state cast
  one whole-mesh shadow. Cutouts, transparency, mismatched state and incomplete
  index coverage keep their original draws. No terrain geometry is removed.
- Routine editor testing and the public build guide now use the optimized native
  editor. Debug remains available for native engine debugging and focused tests.
  Embedded Play uses the editor's build, regardless of standalone Run & Test.

The temporary per-draw timing scope used during diagnosis was removed after
measurement to avoid an allocation per draw. Lighting collection remains a
coarse profiling scope.

## Validation

Physics3D, transform hierarchy, material-library and bgfx renderer tests passed.
New shadow tests cover opaque compaction, transparency, cutouts and incomplete
coverage. Existing hierarchy tests cover composition, cycles and missing parents.
Terrain source/cooked validation and all terrain material regressions are listed
in [surface material qualification](terrain-surface-materials-qualification.md).

The optimized editor's embedded Play profiler showed 3.61 ms average update
(4.05 ms p95) and 6.53 ms render submission (7.13 ms p95) after 707 frames.
Screenshot: `/tmp/terrain-controlled-play-full.png`. GPU timing was unavailable
in that embedded profiler; GPU numbers above come from the standalone run.

The unexplained unsaved deletion around editor startup remains separately
tracked in `tofix.md`. Undo restored a clean scene; the controlled Play-button
retry worked. This performance qualification does not claim that input incident
has been diagnosed.
