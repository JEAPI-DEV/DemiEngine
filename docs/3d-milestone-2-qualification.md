# Milestone 2 qualification — 2026-09-15

Milestone 2 is complete for its declared reference workloads. Target: frame
p95 <= 16.7 ms, p99 <= 25 ms, intact collision and no discarded fixed simulation
time after warmup. All final runs were sequential, 60 seconds with 5 seconds
warmup, VSync off, real deltas and complete visible/animated populations verified.
No builds ran alongside benchmarks. These are scoped synthetic probes, not a
universal 60 FPS or shipping-game guarantee.

## Final desktop checks

Ryzen 7 6800H, Linux Release / X11 / Vulkan. Adapter IDs were checked in telemetry:
NVIDIA RTX 3070 Ti Laptop `10de:24a0`, Radeon 680M `1002:1681`.
Mixed means 1,000 animated capsule bodies plus 1,000 dynamic box props, all active
with contacts reported, plus the static floor. Mixed desktop runs retain the
previously declared 30 Hz far-pose budget beyond 30 world units; the pure animated
run uses full-rate poses. Gameplay clocks, events, root motion and physics remain
full-rate. All use the same geometry-preserving two-skin UAL fixture.

| GPU | Workload | Pixels | Frame p95 / p99 | Result |
|---|---|---|---:|---|
| NVIDIA | 2,000 animated | 1920x1080 | 15.34 / 16.93 ms | Pass |
| NVIDIA | 2,000 mixed | 1920x1080 | 15.93 / 18.49 ms | Pass |
| Radeon | 2,000 mixed | 1920x1080 | 16.30 / 18.95 ms | Pass |
| NVIDIA | 2,000 mixed | 2560x1440 | 16.39 / 18.83 ms | Pass |
| Radeon | 2,000 mixed | 2560x1440 | **16.88** / 19.36 ms | Stretch miss |

The Radeon 1440p miss is retained explicitly. Percentile passes are not perfect
frame locks: individual outliers remain, including about 38.43 ms in that run.
The earlier rigid-body/barrel/tower evidence remains scoped to its recorded
workloads; these final runs do not replace it with an all-entities claim.

Raw reports, per-frame CSVs and binary hashes:
`build/m2-desktop-final-nvidia-animated/`,
`build/m2-desktop-final-{nvidia,radeon}-mixed-{1080,1440}/`.

## Final physical Android checks

Pixel 7, Android 15 / SDK 35, ARM64, Vulkan, native **2400x1080**.
The development profile APK uses optimized `RelWithDebInfo` native code
(`-O2 -DNDEBUG`) and debug signing, not a shipping signed Release artifact.
The user stopped non-system background apps; normal system services stayed on.
Clocks and thermals were not pinned. This is not a long thermal soak.

All Android cases use **full-rate animation**, unchanged geometry and no render
scaling. Mixed counts mean half animated capsules and half dynamic boxes.

| Workload | Objects | Frame p95 / p99 |
|---|---:|---:|
| Animated | 64 | 11.98 / 12.47 ms |
| Animated | 256 | 14.78 / 18.00 ms |
| Mixed | 64 | 11.93 / 12.35 ms |
| Mixed | 256 | 12.05 / 12.89 ms |

All pass. Raw results and per-APK hashes: `build/m2-android-final/`.
The 256-object mixed probe keeps 256 active bodies, 257 total bodies and 384
contact pairs. Neither stopped visual animation nor frozen physics produces
these results. The final crowd and restored gameplay screenshots were inspected.

The decisive fix stops Android swapchain recreation on every usable SUBOPTIMAL
advisory while retaining real resize, out-of-date and surface-loss handling.
The earlier cache-only Debug fix is now maintained in the source patch pipeline.
Sparse-weight GPU skinning and an equivalent directional-only default material
variant address remaining GPU cost. Detailed diagnosis and intermediate failures
are in [the swapchain report](3d-m2-android-swapchain.md).

## Correctness and workflow gate

- 12 focused CTests pass in **both Debug and Release**: skinning/rig/cache tests,
  render/lighting selection, physics, temporal budgeting, profiler callback nesting
  and thread ownership, patch semantics/idempotency/cache migration, and tooling.
- Two desktop animation E2Es pass.
- Three Android animation E2Es pass, including physics at a 1 Hz visual pose
  rate and portrait/landscape transitions with retained crowd and working touch.
- Four Android 2D E2Es pass.
- Both Android projects pass the automatic background/resume probe: retained
  process, process-scoped surface destruction/rebinding markers and screenshot.
- Reference capture checks enforce actual pixels, complete population/playback,
  GPU-path accounting, duration, no focus/interruption/minimization, no physics
  errors, and no meaningful wall/fixed-time loss after warmup.

Gameplay reports: `build/m2-android-gameplay-final/`,
`examples/minimal_2d_android/build/android/qualification/`, and
`examples/animation_3d/build/linux/qualification/`.
This is a focused gate, not a claim that every CTest or every engine feature was
qualified on every platform. Unsupported rigs/material combinations still use
the documented CPU fallback. Automatic mesh decimation, complex animation blends,
large streamed landscapes and production destruction remain later work.
