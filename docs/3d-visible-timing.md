# Visible 1080p timing: dedicated GPU

## Hardware scope and pending iGPU gate

These captures used UEFI configured for **dedicated GPU only**. That testing
uses the GeForce RTX 3070 Ti Laptop GPU, PCI `10de:24a0`, on the Ryzen 7 6800H
development machine. The renderer reports those selected PCI IDs in every
measured window. The display reports 144 Hz, not 60 Hz.

An earlier attempt to force the Radeon Vulkan driver initialized Vulkan but
timed out before completing a capture. Under the dedicated-only firmware setting
this is not valid evidence of Radeon performance or an engine/driver defect.
The user enabled hybrid mode on 2026-09-13; Radeon rendering now works. See
[hybrid graphics testing](3d-hybrid-graphics.md) for selection and new measurements.
The remaining startup, resizing/surface and sustained-performance checklist is in
[plan.md](../plan.md). No firmware changes were made by the agent.

## Diagnostics implemented

- `GraphicsFrameTimings` is backend-neutral data. The bgfx adapter converts
  native timestamp frequencies and filters duplicate/out-of-order GPU frames,
  including frame-number wrap. Missing results remain absent, not zero.
- The shared application context records CPU render preparation, graphics-frame
  advancement and backend wait/elapsed timings. GPU collection does not add a
  synchronous query readback or change frame limiting/VSync policy.
- Raw platform intervals remain separate from the clamped simulation delta.
  Capture flags expose overrides, minimization, interruption and refresh rate.
- `--profile-frames` writes per-application-frame scope totals and gauges, with
  CSV escaping and failure reporting. `--window-size` requests transient desktop
  dimensions without changing authored projects; actual backbuffer pixels are
  verified in the trace.
- The visible lab runner uses normal timer-based auto-quit, temporary project
  variants, real deltas, warmup exclusion and cumulative lost-time analysis.

These are elapsed-time diagnostics, not CPU-utilization measurements. In
particular, backend render-thread time can include driver/presentation waits;
`Graphics.frame_advance` and `Graphics.wait_render` overlap. The precise compositor
presentation latency is not exposed separately by these counters.

## Workload and reproduction

Release build, Vulkan through SDL3, 1920x1080 drawable pixels, 2,000 primitive
bodies plus the floor, fixed simulation step 1/60 s and maximum two catch-up
steps per rendered frame. Same scene, seed, collision/reporting and sleep settings
as the lab. All measured frames reported 2,001 visible meshes and two batches.

Three 12-second runs each for active rigid bodies and piles, with VSync on and
off. The first two seconds were excluded. Delayed GPU samples originating before
that boundary were also excluded. Background desktop activity was not disabled;
no compilation ran concurrently with the measurements.

```sh
python3 scripts/benchmark_3d_visible.py --binary build/linux-release/demi \
  --output build/visible-3d-1080p --counts 2000 --workloads rigid pile \
  --vsync on off --seconds 12 --warmup-seconds 2 --repeats 3
```

Use a new output directory to repeat. The retained directory contains machine
and executable identity, all raw frame CSVs, aggregate reports, logs, and per-run
JSON summaries. VSync-off remained substantially compositor-paced here; it is
not an uncapped hardware-throughput result.

## Measured results

Median of three per-run percentiles, milliseconds:

| Workload | VSync | Frame interval p95 | Frame interval p99 | CPU update p95 | Physics per frame p95 | Render preparation p95 | GPU p95 |
|---|---|---:|---:|---:|---:|---:|---:|
| 2,000 active bodies | On | 10.211 | 12.100 | 7.052 | 4.843 | 3.464 | 0.534 |
| 2,000 active bodies | Off | 8.266 | 11.468 | 5.737 | 4.144 | 2.285 | 0.693 |
| 2,000-body pile | On | 14.497 | 15.446 | 11.895 | 10.261 | 2.387 | 0.827 |
| 2,000-body pile | Off | 14.188 | 15.069 | 11.855 | 10.494 | 2.060 | 0.778 |

GPU and backend samples can lag the application frame; do not add these
percentiles. Physics values sum all fixed steps within a rendered frame and
include zero-step frames. They are not comparable to headless per-step
percentiles as though both measured identical scheduling.

All 12 captures passed the capture-validity checks. Every measured window had
zero discarded fixed-step time. Full-run/startup totals are preserved separately;
startup is not silently counted as steady state. Tiny nonzero clamp sums below
0.001 ms are float-conversion noise, not meaningful simulation loss.

For this particular workload and GPU, all runs meet the provisional p95 <=16.7 ms
and p99 <=25 ms frame-interval targets. This does not qualify complex animated
characters, varied imported assets, destruction bursts, streaming, other hardware,
or the engine's whole 3D roadmap.

## Conclusion

The GPU is not the limiting resource in these primitive scenes: its p95 is below
1 ms, while the pile's CPU simulation/update dominates the active frame cost.
Large graphics advancement/render-thread times must not be mistaken for GPU
execution time. Further optimization should focus on CPU simulation, submission
and representative richer workloads rather than reducing image quality here.

The dedicated-GPU reference is now measured. Hybrid-mode Radeon and physical
Android qualification remain pending.

## Validation

Nine focused Debug CTests and four Release CTests passed, covering timing
conversion/filtering, CSV summaries, profiler output, Noop/device lifecycle,
2D/3D application hosts, physics, and lab validation/smoke where applicable.
CLI checks also verified malformed dimensions, missing trace arguments,
relative/absolute output-path collisions, and write/flush failure propagation
using `/dev/full`. `git diff --check` passed. The full engine suite and physical
Android execution were not run for this diagnostics slice.
