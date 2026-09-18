# Milestone 2: high-resolution and mixed simulation checks

Earlier-stage evidence. See [final qualification](3d-milestone-2-qualification.md)
for milestone closure, updated desktop captures and physical Android results.

2026-09-15. Reference target: 1080p, p95 <= 16.7 ms, p99 <= 25 ms, with no
discarded fixed-step time after warmup. 1440p is measured explicitly, not inferred
from 720p. The desktop reference gate is covered; physical Android qualification
remains open. These are declared synthetic workloads, not a universal game/FPS promise.

## Implementation completed in this slice

- `AnimationPlayer3D.visual_update_rate` (0..240 Hz; zero disables budgeting) and
  `visual_update_distance` (nonnegative world units) provide opt-in temporal LOD.
  The component owns parsing/defaults/metadata; shared schema and Inspector paths
  expose it without an editor-only representation. Round trips retain authored
  fields only, never the internal visual clock.
- A renderer-only schedule distributes refreshes by stable entity ID. Near poses,
  structural changes, clip rewinds, blends/layers and procedural overrides bypass
  the far cadence. Held bone poses are an explicit visual-quality tradeoff;
  root transforms still render each frame.
- Gameplay clocks, events, root motion, input and collision remain full-rate.
  Tests compare event/root-motion traces with budgeting on/off and exercise real
  gravity, ray queries and HUD input with a 1 Hz visual actor.
- Animated meshes can select authored medium/low models through the existing
  MeshRenderer fields. A shared selector serves preparation and drawing. Matching
  named clips/durations are required; unsupported candidates retain the high
  model. Authored model/collider references are not rewritten. Tests use separate
  asset identities backed by the same fixture to check selection, cache refresh
  and missing-clip fallback; this is not a measured polygon-reduction claim or
  an automatic LOD generator.
- The crowd runner adds `mixed`, with half animated capsule bodies and half
  colored box bodies. All are dynamic, with gravity, initial lateral velocity,
  collision reporting and sleeping disabled. Impacts may stop lateral motion:
  awake does not mean perpetually moving. The floor/collider are authored together
  in `crowd.scene.json`, not hidden scene construction in Lua.

## Conditions and results

Linux Release, Ryzen 7 6800H, NVIDIA RTX 3070 Ti Laptop (0x10de/0x24a0) and
Radeon 680M (0x1002/0x1681), X11/Vulkan, VSync off. Models are the same two-skin
UAL1 fixture, with unchanged geometry. Builds and captures were sequential.
Frequencies, thermals and unrelated desktop activity were not controlled.

Full-rate visual animation on NVIDIA (12 seconds per case, 2 seconds warmup):

| Characters | 1080p p95 / p99 | 1440p p95 / p99 |
|---:|---:|---:|
| 64 | 0.88 / 1.00 ms | 1.46 / 1.55 ms |
| 1,000 | 6.96 / 7.58 ms | 7.20 / 7.72 ms |
| 2,000 | 14.37 / 15.39 ms | 14.59 / 15.81 ms |

Mixed simulation below uses **30 Hz far poses beyond 30 world units**. Counts
mean total dynamic objects, not characters: 2,000 means 1,000 characters plus
1,000 props, in addition to the static floor.

| GPU | Resolution | Objects | Duration / warmup | Frame p95 / p99 |
|---|---|---:|---:|---:|
| NVIDIA | 1080p | 1,000 | 20 / 3 s | 6.87 / 7.34 ms |
| NVIDIA | 1080p | 2,000 | 20 / 3 s | 15.36 / 18.24 ms |
| NVIDIA | 1440p | 1,000 | 60 / 5 s | 7.27 / 7.75 ms |
| NVIDIA | 1440p | 2,000 | 60 / 5 s | 16.11 / 18.13 ms |
| Radeon | 1440p | 1,000 | 20 / 3 s | 7.54 / 8.49 ms |
| Radeon | 1440p | 2,000 | 20 / 3 s | **16.83** / 18.72 ms |
| Radeon | 1080p | 2,000 | 60 / 5 s | 16.03 / 18.12 ms |

The Radeon 2,000-object 1440p run narrowly misses the 16.7 ms p95 target. It is
not rounded down or counted as a 60 FPS pass. Larger frame outliers also occur
(for example 46.91 ms in the NVIDIA 1,000-object minute run); percentile passes
do not mean every frame stayed inside budget. A minute is not a thermal soak.

The full-rate mixed NVIDIA 1080p control measured 8.03 / 8.52 ms at 1,000 and
17.49 / 19.29 ms at 2,000 (15-second runs, 3-second warmup). Compare it with the
budgeted runs only as short-run evidence: the visual pose cadence changed, but
physics, object populations and collision settings did not.

All recorded captures passed exact drawable-resolution and complete visibility
checks and reported zero discarded fixed-step time after warmup. Startup losses
remain in raw reports. Mixed checks require the expected native/active-body counts
and contact pairs above the floor-only population. The 2,000-object minute runs
report 2,000 active bodies and 2,978 contact pairs, not sleep-based shortcuts.
Every animated pose is either refreshed or explicitly accounted as deferred.

## Reproduction and retained evidence

```sh
SDL_VIDEO_DRIVER=x11 python3 scripts/benchmark_3d_visible.py \
  --binary build/linux-release/demi --output build/m2-repeat-1440 \
  --counts 1000 2000 --workloads mixed --rig-layout split --skinning gpu \
  --visual-rate 30 --visual-distance 30 --vsync off \
  --seconds 60 --warmup-seconds 5 --width 2560 --height 1440
```

Use a fresh output directory. Use 1920×1080 for the reference resolution and
`--visual-rate 0` for full-rate poses. Radeon selection used
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`, with adapter IDs verified
from telemetry. The helper reimports/validates temporary variants through native
engine services and never edits the checked-in GLB.

Raw per-frame traces, summaries and binary hashes are in these `build/` folders:
`m2-1080p-baseline`, `m2-1440p-baseline`, `m2-mixed-baseline`,
`m2-1080p-budgeted`, `m2-1440p-mixed-minute`, `m2-radeon-1440p-mixed`, and
`m2-radeon-1080p-mixed-minute`.

The mixed captures used a script-created collider matching the visible floor.
The final example moves that same collider onto the authored floor entity so
editor transforms cannot leave physics behind. Pure animation now also includes
that one static floor body. No dynamic population or collision quality was reduced.

## Remaining qualification

The Android debug APK now passes physical functional checks described below.
Milestone 2 remains open for optimized Android scaling/performance measurements.
The subsequent [optimized phone probes](3d-m2-android-scaling.md) now provide
native-resolution traces, but background app updates prevent clean qualification.
More elaborate materials, rigs,
blending, AI and long thermal/memory soaks are not qualified by these fixtures.

Validation passed: six focused Debug CTests, four Release CTests, the benchmark
summary/fixture tests (12 Python cases), and two desktop E2E cases. Coverage
includes metadata/schema/round trips, cadence limits, near-camera bypass, clip
and clock changes, gameplay events/root motion, compatible/incompatible model
LOD selection, gravity/collision and HUD input. This is not a full-suite result.

The final authored-floor mixed scene was visually checked at 1080p; its capture
is `build/m2-mixed-visual-review.png`. That screenshot run is excluded from the
performance tables. The current Android debug artifact is
`examples/animation_3d/build/android/3d_animation-debug.apk`; it was rebuilt after
the animated-model LOD changes and was used for the device checks below.

## Physical Android functional follow-up

Pixel 7, Android 15 / SDK 35, ARM64, Vulkan. Sequential checks with Debug APK
SHA-256 `4f0b871bca42ff96f0e8cb4ccf9e11c03f733ee6ecde39a7f0036ac20d84443c`:

- Both animation E2Es passed: gravity/collision and HUD input with a 1 Hz visual
  actor, plus repeated crowd scene entry/exit. No session crash markers.
- Manual Home/background and foreground retained the same process; logs show
  surface generation 2, renderer native-window rebinding and loop resumption.
  Touch then opened the live crowd successfully.
- Inspected the 64-character crowd in portrait and at native 2400x1080 landscape.
  Portrait framing clips the outer columns; landscape shows the complete grid.
  This is an example camera/layout limitation, not a complete-visibility
  portrait performance pass. Rotation settings were restored and the app stopped.

Evidence: `build/m2-android-functional/qualification.json`, `resumed.png`,
`landscape.png`, and `lifecycle-logcat.txt`. The automated report covers the E2Es;
background/resume and orientation were separate manual checks. No optimized
frame traces, multi-skin/model-LOD device comparison, sustained thermal test or
large mixed-body Android run is claimed here.

The device runner initially guessed the APK name from the directory and required
unrelated 2D-demo markers. It now follows native display-name artifact naming,
retains demo-specific requirements only for that probe, and stops reporting
untested generic feature coverage. Six focused Python runner tests pass.
