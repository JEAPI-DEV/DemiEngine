# Android landscape swapchain investigation

2026-09-15, Pixel 7 / Android 15 / Vulkan, optimized native profile build.

## Measured cause

The 64-character two-skin probe at 2400x1080 recreated its swapchain in every
measured frame. In `build/m2-android-swapchain-recreation/animated-64/`, frame
p95 was 26.75 ms, while swapchain creation alone was 17.91 ms p95. The creation
scope appears in all 622 measured frames. This work precedes bgfx's CPU render
timer, explaining the misleadingly small `Graphics.render_thread` measurement.

Earlier diagnostic captures under `build/m2-android-{vulkan-waits,bgfx-frame-scopes,
memory-query,submit-tail,uniform-flush}/` ruled out the suspected direct API costs:
acquisition, presentation, queue submission, command fences, timer readback,
staging flushing and uniform flushing were small. The memory-budget query was
not executing on this driver. Scopes are nested and must not be added together.

The cached Debug bgfx checkout contained an older, local-only SUBOPTIMAL fix;
the maintained dependency patch pipeline did not. A fresh optimized checkout
therefore had different behavior. The new patch explicitly supports both the
pinned original source and that exact older local edit, preserving other changes
and failing on unknown source shapes.

## Correctness boundary

Android's compositor can keep a landscape swapchain usable while reporting
`VK_SUBOPTIMAL_KHR`. Recreating the same swapchain on every advisory does not
resolve that situation. The Android policy now treats that result as usable for
both acquisition and presentation; it retains semaphore/fence handling and all
explicit resize/reset, out-of-date and surface-loss recreation paths. Desktop
policy is unchanged.

This follows the [Vulkan WSI contract](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html):
SUBOPTIMAL acquisition succeeds and its image can be presented; OUT_OF_DATE
requires recreation. This does not implement pre-rotated rendering or eliminate
the compositor's orientation work.

## Instrumentation

The development `profile` variant enables the pinned bgfx profiler callbacks.
`BgfxProfileCallback` records selected nested scopes on its owning submission
thread only, with bounded stack storage. It does not change synchronization.
Shipping builds retain the default callback behavior. Screenshots still use the
device capture path. Diagnostics-only source patches add missing timing scopes;
the separate suboptimal patch owns the behavior change.

## Qualification after the fix

The source-controlled policy removes steady-state recreation on the Pixel 7:
SUBOPTIMAL advisories are still observed, but no swapchain-creation scope occurs
after warmup. All four final native 2400x1080 runs lasted 60 seconds with a
5-second warmup, full-rate animation, unchanged meshes and collisions, and no
discarded fixed simulation time. No temporal LOD or render scaling was used.

| Workload | Objects | Frame p95 / p99 |
|---|---:|---:|
| Animated | 64 | 11.98 / 12.47 ms |
| Animated | 256 | 14.78 / 18.00 ms |
| Mixed | 64 | 11.93 / 12.35 ms |
| Mixed | 256 | 12.05 / 12.89 ms |

The swapchain fix alone brought the 64-character case to 11.94 ms p95. The
256-character case still needed GPU work reduction: the default material now
selects an equivalent directional-only fragment variant when no local lights
are active, and skinning skips genuinely zero-weight trailing influences.
The full local-light fragment binary remained identical; tests cover all four
point/spot slots and switching the renderer's selected default program.
No small weights are discarded or renormalized. Roughly 88% of the probe's
vertices have only one or two influences.

Intermediate probes and limits remain recorded under
`build/m2-android-{suboptimal-fixed,qualified-budget,directional,sparse-influences}/`.
The far-pose-rate experiment alone did not meet the 256-character target; the
final results use full-rate poses. Final raw evidence and APK hashes are under
`build/m2-android-final/`.

The updated Android animation example passes three gameplay/rotation E2Es plus
the separate automatic background/resume probe. The 2D Android example passes
four E2Es and the same resume probe. Logs confirm retained processes and native
surface rebinding; the resumed 3D screenshot was inspected. Reports live under
`build/m2-android-gameplay-final/` and
`examples/minimal_2d_android/build/android/qualification/`.

Twelve focused CTests pass in both Linux Debug and Release, including the
bounded/thread-owned callback, patch policy/idempotency/cache migration, GPU
skinning, renderer/local-light selection, pose budgeting, physics and trace
validation. The desktop animation E2Es also pass. This is not a full-suite,
all-device or long thermal-soak claim. Desktop final scaling checks and milestone
closure are recorded in [the qualification summary](3d-milestone-2-qualification.md).
