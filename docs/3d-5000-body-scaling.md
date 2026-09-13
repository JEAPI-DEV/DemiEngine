# 5,000-body scaling probe — 2026-09-13

## Scope and setup

This extends the existing primitive performance lab to 5,000 bodies; it is not a
recreation of the Crysis barrel model or video. The lab's editor range and both
benchmark runners now accept 1–5,000, while their usual defaults remain unchanged.
The camera, floor, primitive geometry, and per-body behavior were not changed.

Blender was running throughout at the user's request (process confirmed before
testing); its resource use was not isolated or measured continuously. The laptop
was on AC power in hybrid mode. No firmware, display, power-policy, solver, CCD,
sleep, or fixed-timestep settings were changed. These are results under that
background workload, not an attribution of slowdown to Blender.

The Release visible sweep runs 2,000, 3,000, and 5,000 bodies with both `rigid`
and `pile` workloads, three repetitions each, VSync requested on, a 12-second
game-timer duration, and two-second timing warmup. NVIDIA runs before Radeon;
within each GPU the count order is 5,000, 2,000, 3,000. The logical 1440×810 window
request targets the laptop's 133%-scaled panel; actual 1920×1080 pixels and 165 Hz
are independently checked. No builds or other benchmarks run concurrently.

`rigid` starts bodies moving vertically without gravity, leaves collisions on,
and disables sleeping. Contacts can stop bodies: active-body count is not proof
that every body is still moving. `pile` applies gravity and permits natural
sleeping. Both retain contact reporting, collision response, and the normal
1/60-second physics timestep with at most two catch-up steps per rendered frame.

## A capacity failure found before qualification

The first 5,000-body pile pilot hit Jolt update errors on 218 physics steps.
The old integration discarded the update return value, so this failure would
not have been apparent from FPS alone. Jolt documents these errors as capacity
overflows that can cause contacts/pairs to be ignored. This pilot is excluded.

`Physics3D.update_error_steps` now records a cumulative error-step count per
physics world. The visible runner scans the entire trace, including startup,
and rejects nonzero or missing error telemetry. Warmup excludes startup timing,
not physics correctness failures. Tests cover missing counters, update errors,
and an error occurring only before warmup; native tests check healthy telemetry.

Native contact capacity was increased from 10,240 to 32,768 per world. Body and
body-pair capacities remain 65,536. This increases per-world memory requirements,
not collision simplification or solver quality. The limits remain finite and
arbitrary denser scenes may still overflow; the counter must remain observable.
The corrected runs must show zero errors before being used for comparison.

The rejected pilot is retained in `build/scaling-5000-pilot/`. Corrected captures
are in `build/scaling-5000-nvidia/` and `build/scaling-5000-radeon/`, including raw
frame traces, logs, aggregate timings, summaries, and executable hashes.

## Results

All 36 corrected captures verified the intended PCI GPU IDs, 1920×1080 pixels,
165 Hz, no focus loss/minimization/interruption, zero physics update errors, and
the full population plus the floor visible in two batches with zero meshes culled.
Blender remained open. This is measurement validity, **not** a timing pass.

Each value below is the median of three per-run measurements after warmup. FPS
is measured frames / wall seconds, not the reciprocal of p95. Frame and physics
columns are p95 milliseconds; physics/frame may include two catch-up steps.
Lost time is fixed-step time discarded during roughly ten measured wall seconds.

| GPU | Bodies | Workload | Average FPS | Frame p95 | Physics/frame p95 | Lost time (ms) |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| NVIDIA | 2,000 | Moving | 163.6 | 8.63 | 4.06 | 0 |
| NVIDIA | 2,000 | Pile | 119.5 | 15.02 | 9.58 | 0 |
| NVIDIA | 3,000 | Moving | 139.3 | 11.57 | 5.75 | 0 |
| NVIDIA | 3,000 | Pile | 29.9 | 40.19 | 31.82 | 700 |
| NVIDIA | 5,000 | Moving | 39.8 | 33.55 | 20.55 | 50 |
| NVIDIA | 5,000 | Pile | 15.0 | 74.92 | 59.85 | 4,983 |
| Radeon | 2,000 | Moving | 161.6 | 9.19 | 4.00 | 0 |
| Radeon | 2,000 | Pile | 121.5 | 14.48 | 9.30 | 0 |
| Radeon | 3,000 | Moving | 124.0 | 12.73 | 5.95 | 0 |
| Radeon | 3,000 | Pile | 30.5 | 39.42 | 30.95 | 650 |
| Radeon | 5,000 | Moving | 37.2 | 35.34 | 21.19 | 67 |
| Radeon | 5,000 | Pile | 13.1 | 84.99 | 62.11 | 5,650 |

All 2,000-body cases and all 3,000-body moving cases discarded zero measured fixed
time. Every 3,000-body pile and every 5,000-body case discarded some. Median
physics advancement / scaled input time was about 93% for 3,000-body piles,
99.4% for 5,000-body moving workloads, and 49.9% / 43.6% for 5,000-body piles
on NVIDIA / Radeon. Window-edge accumulator carry can yield ratios slightly
above 100% in healthy short captures; use the explicit lost-time counters too.

At 5,000 moving bodies, NVIDIA's individual run averages ranged from 29.0 to
40.0 FPS. A near-40 FPS median therefore does not establish stable real-time
32 FPS, let alone 60 FPS. The pile's slowdown is much larger. GPU p95 medians
at 5,000 were 1.81 / 2.86 ms for moving bodies and 6.14 / 2.92 ms for piles
(NVIDIA / Radeon). Neither GPU's execution time accounts for most of the frame.
Different GPU clocks/background activity are not isolated; don't interpret the
pile GPU difference as a general hardware ranking.

## Why the pile falls off sharply

In NVIDIA pile repeat 1, the average cost of an individual physics step rose
from 8.39 ms at 2,000 to 14.06 ms at 3,000 and 27.11 ms at 5,000. At 3,000,
adding gameplay/render preparation to that physics cost frequently pushes the
frame beyond one 16.67 ms timestep; subsequent frames then need two physics steps.
At 5,000, even physics alone exceeds a 60 Hz step budget.

The measured post-warmup step counts show that feedback:

- 2,000: 589 one-step frames, three two-step frames, 591 frames without a step.
- 3,000: 36 one-step frames, 259 two-step frames, four without a step.
- 5,000: every measured frame (149) takes two steps and still falls behind.

At 5,000, average per-step costs include 11.40 ms in native simulation including
our listener, 7.45 ms in contact publication/phases, 4.34 ms in body synchronization,
and 2.81 ms in component synchronization. Further work should target per-step
integration/contact costs as well as native simulation; merely increasing the
catch-up limit or lowering the simulation rate would not demonstrate the requested
performance. The existing catch-up policy was deliberately left unchanged.

## Validation and limits

Release physics and summary tests passed. Debug physics, bgfx 3D host, and summary
tests passed; the example validates. Both runners reject 0 and 5,001 counts.
The 5,000-body headless runner also completed a short smoke test, not a performance
qualification. The source diff passes whitespace checks.

This is a short primitive stress test with Blender open, not a thermal soak,
imported-barrel asset test, animated-character benchmark, Android memory test, or
full-game qualification. The larger per-world native allocation's Android memory
impact remains unmeasured. No Blender model was needed for this comparison.

## Reproduction

Use the wrapper in [hybrid testing](3d-hybrid-graphics.md#evidence-and-reproduction)
with fresh output directories and these benchmark arguments:

```text
--counts 5000 2000 3000 --workloads rigid pile --vsync on
--seconds 12 --warmup-seconds 2 --repeats 3 --width 1440 --height 810
```

Select each ICD explicitly and validate actual 1920×1080 backbuffer dimensions;
the 1440×810 request is logical size at this panel's scaling, not a lower-resolution
render test. Keep the cursor on the laptop display when launching, leave windows
untouched, and document background applications. `valid_capture` is evidence
validity, not a performance or real-time simulation pass. Headless timings do not
establish real-time throughput; also inspect their update-error gauge.
