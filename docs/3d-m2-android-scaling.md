# Milestone 2: optimized Android scaling probes

Historical investigation record. The later [final qualification](3d-milestone-2-qualification.md)
closes Milestone 2 after the swapchain and shader fixes; the failures and open
items below describe earlier checkpoints, not the current milestone status.

2026-09-15. Pixel 7, Android 15 / SDK 35, ARM64, Vulkan, native 2400x1080
landscape. These are **provisional measurements, not a completed performance
qualification**: Android was installing unrelated app updates during this batch.
The user was asked to pause those updates before clean repeat/control runs.

## Reusable measurement path

The new development-only Gradle `profile` variant uses native `RelWithDebInfo`.
The actual CMake cache confirms `-O2 -g -DNDEBUG` and
`-DDEMI_ANDROID_PROFILE=1`. Java debugging/debug signing remain enabled for
device inspection; this is not a shipping Release APK. No release credentials
or shipping signing changes are needed.

`scripts/benchmark_android_crowd.py` reuses the existing crowd fixture generator,
native validation/cook/package audit, runtime frame profiler, and desktop trace
summary/population checks. Only already audited cooked assets are repackaged.
Gradle regenerates its platform-owned asset index. The runner has a separate
app identity, sequential build/run phases and fresh output directories.
The profiler uses fixed app-private paths and is opt-in only in this variant.

Reproduce after pausing background updates, with the device unlocked:

```sh
python3 scripts/benchmark_android_crowd.py \
  --output build/m2-android-repeat --counts 64 256 \
  --workloads animated mixed --seconds 30 --warmup 5
```

## Provisional results

30-second runs, 5-second warmup, VSync off, full-rate independent poses. Same
8,546-vertex / 13,744-triangle UAL character and geometry-preserving two-skin
fixture as desktop. No mesh simplification, visual-rate reduction, resolution
reduction or sleeping-body shortcut. Counts in mixed cases mean half animated
capsules and half box props, with one additional static floor.

| Workload | Objects | Frame p95 / p99 | GPU p95 | CPU render preparation p95 |
|---|---:|---:|---:|---:|
| Animated | 64 | 32.26 / 34.01 ms | 12.91 ms | 5.66 ms |
| Animated | 256 | 47.56 / 49.90 ms | 26.03 ms | 12.93 ms |
| Mixed | 64 | 25.04 / 26.34 ms | 7.84 ms | 3.85 ms |
| Mixed | 256 | 34.97 / 37.33 ms | 21.42 ms | 8.05 ms |

All four captures passed dimensions, visible population, GPU-skinning population,
playback, completion and native physics-error checks. They reported zero discarded
fixed time after warmup; startup losses remain in the raw results. The 256-object
mixed case retained all 256 active bodies, 257 total bodies and 384 contact pairs.
Its physics p95 was 6.08 ms. None of these provisional frame results passes a
16.7 ms p95 target. Mobile budgets are not silently relaxed to declare a pass.

Graphics frame advancement dominates several traces: the 64-character case has
26.23 ms p95 in `bgfx::frame`, versus 12.91 ms measured GPU work. These scopes
are not additive. The gap points to presentation/driver/synchronization work for
further investigation, but does not identify a specific fence or justify removing
Android's surface-lifecycle synchronization. Larger crowds also increase actual
GPU and CPU pose costs. Background updates prevent a clean causal comparison.

## Platform issue found and fixed

SDL's default policy for resizable windows overrode the Android manifest's
landscape setting, rotating games into portrait. `DemiActivity` now retains the
current explicit orientation when SDL supplies no explicit orientation hint.
This includes authored policy and explicit gameplay requests; unspecified policy
and explicit SDL hints retain SDL handling. Vendored SDL code is untouched.
The first portrait capture is rejected as wrong-resolution evidence, not counted
as a faster 1080p result.

## Evidence and remaining work

- `build/m2-android-landscape/<workload>-<count>/`: raw per-frame CSV, aggregate
  CSV, JSON summary, APK SHA-256, build log, launch screenshot, and before/after
  thermal-service snapshots. Snapshots are not a thermal soak.
- `build/m2-android-optimized-v2/`: earlier rejected portrait probe.
- Three benchmark-tool tests, six Android runner tests and twelve trace/fixture
  tests pass; profile APK compilation passes. This is not a full CTest suite.
- The four landscape captures exercised the manifest-preserving orientation fix.
  Its final refinement preserves the current request (including gameplay overrides)
  rather than re-reading the manifest. That refinement compiles, but its final
  device-install attempt stalled and the host install command was cancelled.
  Recheck explicit gameplay orientation changes on-device in the next gate.
  The profiling app was stopped; no unrelated app or update was stopped.
- Next: quiet-device repeat and frozen/temporal-budget controls, then target the
  measured rendering bottleneck. Validate any renderer synchronization change
  against surface destruction/resume before retaining it. Milestone 2 remains
  open; desktop qualification does not imply mobile parity.

## Follow-up after user background-app cleanup

The user closed non-system applications. Normal system services remained active;
disabling them is neither required nor requested. Same optimized variant, native
2400x1080, full-rate two-skin fixture, sequential runs, 5-second warmup. CPU/GPU
clocks and thermals were not pinned, so these are repeatable workload captures,
not a claim of a perfectly controlled device.

| Case | Duration | Frame p95 / p99 | GPU p95 |
|---|---:|---:|---:|
| 64 animated characters | 30 s | 27.14 / 28.54 ms | 14.20 ms |
| 64 frozen characters | 30 s | 29.61 / 33.15 ms | 14.14 ms |
| 256 mixed objects | 60 s | 31.70 / 33.85 ms | 21.35 ms |

All passed capture validation and lost no fixed simulation time after warmup.
There were no unfocused or interrupted measured frames. The mixed run retained
256 active bodies, 257 total bodies and 384 contact pairs; physics p95 was
5.84 ms. The refined orientation policy now builds, installs and retains the
correct landscape drawable on-device. Explicit gameplay orientation changes
are still not a separately exercised test.

The frozen control reduced CPU render preparation p95 from 7.08 to 3.86 ms,
but did not improve end-to-end frame time. GPU work stayed almost unchanged.
This argues against pose evaluation being the primary cause of this particular
64-character shortfall; it does not mean animation preparation is free.

An equivalent skinning shader rewrite evaluated only the three matrix columns
needed for normals. A valid replay measured GPU p95 14.08 ms versus 14.20 ms
baseline, without a convincing GPU improvement beyond variation. The rewrite
was reverted; no geometry, animation cadence, collision quality or resolution
was reduced. Its first capture overlapped an installation and failed validity;
only the separate replay was considered. APK installation now uses standard
ADB non-streaming mode after repeated streamed-install stalls; no device settings
or system applications were changed.

Evidence: `build/m2-android-quiet-controls/{animated,frozen}-64/`,
`build/m2-android-quiet-mixed/mixed-256/`, and rejected-optimization replay
`build/m2-android-normal-columns-replay/` (APK in
`build/m2-android-normal-columns/animated-64/profile.apk`). The final mixed run
uses the restored original shader. Three benchmark-tool, six runner and twelve
trace/fixture tests pass; `git diff --check` is clean.

**Next:** add profile-only backend timing for swapchain acquisition, presentation
and command-buffer/fence waits to explain time inside `Graphics.frame_advance`.
Actual GPU cost also matters, especially at 256 objects. Do not remove Android
surface synchronization speculatively. Milestone 2 remains open because these
workloads still miss the 16.7 ms p95 goal, not because the user needs to stop
more system services.
