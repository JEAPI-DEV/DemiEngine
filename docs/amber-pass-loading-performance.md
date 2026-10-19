# Amber Pass loading, riding and performance

## Scope and result (2026-10-19)

The demo now starts in a small illustrated loading scene. It keeps presenting
while the next world, collision shapes and visual resources are prepared. The
progress bar reports completed work, not an elapsed-time animation. Native
source and cooked E2E tests both reach the valley, steer in both directions,
ride, stop and remain grounded.

This is still an experimental lightweight 3D scene. These changes do not bring
its vegetation, atmosphere or rendering efficiency to Elden Ring parity.

## Ownership and defects corrected

- `SceneFlow` prepares a world once and retains cancelled worker jobs until they
  finish. Destroying a live future must not block the presentation thread.
- Physics prepares immutable Jolt shapes off-thread and adopts them on the
  simulation thread. Repeated scaled props reuse model geometry; preparation
  does not construct a second physics simulation. Worker durations are gauges,
  not frame-thread profiler scopes.
- `AssetGroup` retains stable upload batches. The registry adapter distinguishes
  pending publication from failure. CPU decoding and mip construction run on a
  worker; GPU publication advances between frames and commits transactionally.
- Source verification streams through a bounded buffer. The headless visual
  adapter verifies sources without retaining encoded models/images as gameplay
  text, avoiding a false 64 MiB decoded-payload budget failure.
- Presentation readiness includes incoming-camera terrain resources and the
  sky's actual linear-color texture variant. Embedded Play uses the same staged
  asset and readiness boundaries, with lifecycle hooks injected before startup.
- SVG textures retain intrinsic dimensions, while icons retain their established
  square canvas. Optional librsvg now requires version 2.52 or newer.
- A/D now follow the chase-camera screen convention. Separate braking stops a
  walk in about 0.10 seconds and a gallop in about 0.225 seconds. Reversal brakes
  through zero first; collision remains handled by CharacterController3D.
- Jolt's supporting plane previously used the visual origin. Offset capsules
  could stand on terrain yet lose grounded state after movement. The plane now
  uses the actual collider center, tested with offset capsules and boxes.
- E2E swipes previously consumed a nominal 60 Hz event count at the actual
  render rate. They now interpolate over elapsed runtime seconds. Directional
  assertions were preserved; native source/cooked tests now agree.

## Rendering findings

Orthographic culling confused full height with half height, admitting an
unnecessarily wide shadow region. Instance positions now have a revisioned
spatial index queried independently for each camera and shadow cascade. Bounds
include geometry, scale and wind; offscreen shadow casters remain eligible.
A scratch bitmask preserves authored instance order without sorting thousands
of accepted IDs every view. Opaque surfaces submit near-to-far.

Plain identical PBR surfaces can also share a batch across entity owners.
Owner-specific overrides remain separate. The focused renderer regression
proves that identical owners merge and different tint/tiling does not leak.
However, this last change did **not** reduce Amber Pass's batch count; it is not
a demonstrated performance win for this scene.

The profiler now reports main and shadow triangle counts separately. Earlier
qualification text incorrectly described the main counter as including shadows.
At the final frozen camera, main geometry was 1,686,195 triangles and the three
shadow cascades added 5,371,668 triangles. Main batches were 317; shadow batches
were 411. The apparent on-screen density substantially understates total work.

## Measurements

Linux Release, SDL3/X11, Vulkan, RTX 3070 Ti Laptop GPU, AC power. One native
runtime at a time; no build or Blender render during measurements. Samples below
exclude startup (frames 180–599). Frame and GPU timings overlap and must not be
added. CPU render preparation can include submission backpressure; it is not
pure CPU execution time.

An earlier matched 1920 × 1080, 65-metre grass-view comparison isolated the
visibility scratch/cache changes:

| Median milliseconds | Before | After |
| --- | ---: | ---: |
| Frame total | 9.245 | 9.156 |
| GPU | 9.509 | 9.396 |
| Render preparation | 8.090 | 6.969 |
| Instance extraction | 2.945 | 1.737 |

Instance upload remained 1,653,072 bytes. This reduced CPU extraction, without
a material GPU improvement. Traces: `/tmp/amber-baseline.csv` and
`/tmp/amber-current.csv`. The before executable already included the first
spatial-index and orthographic-culling corrections.

The final cross-owner batching comparison used the same frozen scene at
3840 × 2022, with all 30,637 grass clumps retained and the visibility limit
extended to 160 metres. Full/medium/low geometry changes at 10/35 metres.
Three shadow cascades and 4× MSAA remain enabled.

| Milliseconds | Before median / p95 | After median / p95 |
| --- | ---: | ---: |
| Frame total | 13.415 / 16.274 | 13.228 / 15.557 |
| GPU | 12.890 / 15.011 | 12.131 / 15.849 |
| Render preparation | 12.739 / 15.162 | 12.622 / 14.049 |
| Instance extraction | 3.733 / 5.251 | 3.809 / 5.202 |

Both snapshots reported 1215/5486 MHz graphics/memory clocks, approximately
80 W and 100% GPU utilization. Batch counts and geometry were identical. These
single-run differences are too small/inconsistent to establish a batching speedup.
Traces: `/tmp/amber-batch-before.csv`, `/tmp/amber-batch-after.csv`.

A separate diagnostic run temporarily disabled grass, shadows and MSAA in turn.
GPU samples fell from roughly 9–11 ms to 3.4 ms without grass, or 2.8 ms without
shadows; removing MSAA made a smaller difference. These are overlapping effects,
not additive costs, and clocks varied. The shipping example retains all three.
This identifies foliage/shadow rendering as the next investigation target.
Trace: `/tmp/amber-ablation.csv`.

A complete native loading run recorded a maximum update of about 35 ms, compared
with an earlier approximately 256 ms activation spike. This is not a hard frame
budget: individual mesh/texture GPU uploads are indivisible. The measured final
loading run is `/tmp/amber-final-4k.csv`; artwork capture is
`/tmp/amber-loading-final-3.jpg`. Initial graphics/window setup still precedes the
first loading frame.

## Comparison with established engines and next work

Unreal documents hardware-instanced foliage batches, cluster occlusion and
per-instance distance fading in its [foliage workflow](https://dev.epicgames.com/documentation/en-us/unreal-engine/foliage-mode-in-unreal-engine).
Unity documents depth-based GPU occlusion with its GPU Resident Drawer in
[GPU occlusion culling](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/gpu-culling.html).
These explain useful architectural directions; they do not establish Elden
Ring's internal implementation from screenshots.

Demi's spatial index is CPU visibility selection, not GPU occlusion. Persistent
GPU instance transforms, indirect visible draws and depth-based occlusion remain
unimplemented. Further work should measure shadow-specific foliage policy and
alpha overdraw, then evaluate fade/coverage and distant representations while
preserving close-up density. Increasing grass distance alone does not solve these
costs. The retained profiler counters make these tradeoffs measurable.

## Validation and remaining boundaries

Release builds and focused suites passed for scene visibility, 2D/3D rendering,
texture preparation, asset streaming/services, scene/prefab preparation, editor
Play lifecycle, physics, Lua E2E timing, scene loading and runtime failure lifetime.
The standalone Lua riding regression covers 30/60/120 Hz, three headings,
reversal and stopping distance. Native source and cooked riding E2E both passed.
Amber Pass validates 134 files without diagnostics. This is not a full repository
test-suite qualification.

Native manual embedded Play interaction remains unverified in this round: desktop
pointer positioning was unreliable, so the automation refused the uncertain
click. Embedded lifecycle tests passed; standalone native rendering exercised the
shared preparation backend.

Large procedural prefab adoption still has a main-thread phase. Single GPU
uploads can exceed a scheduling slice. Releasing a heavy outgoing scene may
still rebuild registry state and invalidate warm resources. The tested demo
starts from a lightweight loading scene with its background explicitly preloaded.
Headless checks do not qualify visuals. Website templates were updated locally;
this work has not deployed documentation or packages online.

Reproduce the public workflow from the repository root:

```sh
./build/linux-release/demi validate examples/amber_pass_3d
./build/linux-release/demi test linux --project examples/amber_pass_3d
./build/linux-release/demi run linux --project examples/amber_pass_3d \
  --window-size 3840x2022 --max-frames 3000 \
  --profile-frames /tmp/amber-profile.csv
```

For controlled comparisons, freeze the camera and remove controller movement in
a temporary project copy; keep the same assets and settings for both executables.
Wait for scene readiness and steady clocks before selecting the sample interval.
