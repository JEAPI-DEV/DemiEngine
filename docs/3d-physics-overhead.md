# Physics integration overhead — 2026-09-13

## Diagnosis

The slowest Radeon run in the [hybrid baseline](3d-hybrid-graphics.md),
`pile-2000-off-2`, spent 44% of accumulated physics time in `Physics3D.simulate`,
26% in contact publication/phases, and 19% in body synchronization. The simulation
scope includes Jolt's calls into our contact listener, not just native solving.
After warmup, 581 frames had one physics step, seven had two, and 479 had none.
Catch-up work was uncommon; repeated integration overhead was worth addressing
before changing solver configuration or worker counts.

## Implementation

- `PhysicsWorld3D` stores internal contact pairs using two generation-bearing
  native body IDs packed into a 64-bit key. Worker callbacks no longer copy entity
  names, concatenate pair strings, or store strings in raw contacts while holding
  the contact mutex. New map entries can still allocate; this is not a lock-free
  or completely allocation-free contact path.
- Native IDs remain private to the Jolt adapter. Publication resolves current
  entity IDs through the epoch/generation-checked frame table and sorts borrowed
  canonical entity-ID pairs. Public ordering and directional reports remain based
  on stable authored identities, not native allocation order.
- Removal callbacks reject retired/recycled handles before querying Jolt.
  Sleeping-contact retention and reporting opt-outs remain supported.
- Body cleanup uses that same frame table rather than constructing and destroying
  a duplicate string-ID hash set every fixed step.
- `PhysicsContactPhases3D` merges sorted pair groups once, assigning both reporting
  directions together and appending exits in previous-event order. Unsorted/mixed
  character contacts retain the general fallback. No new public API is needed.

Collision shapes, filters, solver iterations, sleep policy, CCD settings, fixed
timestep, catch-up limits, and gameplay callbacks were not relaxed. There are no
example-specific workarounds or scene changes.

## Headless measurements

Release, 2,000 bodies, three runs per workload, 720 frames per run; the aggregate
profiler retains its last 600 scope calls. These CPU measurements are not visible
FPS or wall-clock simulation-speed qualification. Table values are medians of
per-run p95 values, in ms.

| Pile scope | Before | Native contact keys only | All changes |
| --- | ---: | ---: | ---: |
| CPU frame | 13.523 | 12.927 | 11.415 |
| Physics step | 11.744 | 10.530 | 9.331 |
| Simulation, including listener | 5.400 | 4.211 | 4.145 |
| Body synchronization | 2.114 | 2.226 | 1.819 |
| Contact publication/phases | 3.336 | 3.173 | 2.577 |
| Contact phase assignment | 1.586 | 1.593 | 0.978 |
| Contact sorting | 0.888 | 1.008 | 0.984 |

The final median CPU-frame p95 improved by 15.6%; physics-step p95 by 20.5%.
Sorting became slightly more expensive, but eliminating contact-callback strings
and reducing other bookkeeping outweighed it. Moving-body CPU-frame p95 changed
from 5.355 to 4.688 ms. Run-to-run variation remains: final pile CPU-frame p95
ranged from 10.289 to 12.468 ms, versus 13.026 to 13.830 ms before.

Evidence directories: `build/physics-overhead-before/`,
`build/physics-native-contacts/`, and `build/physics-overhead-after/`. Each includes
binary hashes, machine metadata, reports, and logs. Reproduce with a fresh output
directory:

```bash
python3 scripts/benchmark_3d_lab.py --binary build/linux-release/demi --output build/physics-overhead-repeat --counts 2000 --workloads rigid pile --repeats 3
```

## Visible comparison on both GPUs

The same 24-capture procedure as the hybrid baseline was repeated after all
changes: 2,000 bodies, 12 seconds, two-second warmup, three runs per workload and
VSync setting. All captures verified the intended PCI IDs, 1920×1080 backbuffers,
165 Hz laptop display, 2,001 visible meshes in two batches, GPU timestamps, normal
deltas, and no focus loss, minimization, or interruption. No builds or other
benchmarks ran concurrently. Power/thermal conditions and background services
were not isolated, so smaller timing differences should not be overinterpreted.

Medians of per-run p95 values, ms:

| GPU | Workload | VSync | Frame before | Frame after | Physics/frame before | Physics/frame after |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Radeon | Moving bodies | On | 9.597 | 8.926 | 4.352 | 3.871 |
| Radeon | Moving bodies | Off | 9.989 | 8.398 | 4.394 | 3.867 |
| Radeon | Pile | On | 14.288 | 13.595 | 10.651 | 9.320 |
| Radeon | Pile | Off | 15.354 | 13.775 | 10.947 | 9.248 |
| NVIDIA | Moving bodies | On | 9.446 | 8.383 | 4.231 | 3.778 |
| NVIDIA | Moving bodies | Off | 9.204 | 8.770 | 4.283 | 4.121 |
| NVIDIA | Pile | On | 15.608 | 14.053 | 10.917 | 9.377 |
| NVIDIA | Pile | Off | 15.644 | 14.122 | 10.868 | 9.264 |

Worst per-run frame p95 changed from **17.037 to 14.362 ms on Radeon** and
**15.757 to 15.411 ms on NVIDIA**. All 24 runs now meet p95 ≤16.7 ms and p99
≤25 ms, with zero post-warmup fixed-step time discarded. Wall-clamp totals were
below 0.0001 ms per GPU batch. This does not claim zero startup time loss.

Worst per-run p99 was 15.826 ms on Radeon and 18.823 ms on NVIDIA. NVIDIA's p99
did not improve over the baseline's 17.547 ms; isolated hitches remain. Individual
frame maxima were 29.513 and 38.166 ms. The aspirational all-runs p95 below 14 ms
is **not** achieved. A percentile pass is not a hitch-free or long-duration gate.

Radeon GPU p95 medians stayed around 2.1 ms. NVIDIA GPU p95 medians were
0.23–0.30 ms in this batch, versus 0.40–0.48 ms before. Rendering code did not
change; do not attribute that GPU timing difference to these CPU optimizations.
Physics remains the largest measured CPU cost in the pile. Further work should
measure residual contact/synchronization work and native simulation variability,
then cover a thermal soak and representative animated/material-heavy scenes.

Raw evidence is retained in `build/physics-overhead-visible-radeon/` and
`build/physics-overhead-visible-nvidia/`. Reproduce with the wrapper in
[hybrid graphics testing](3d-hybrid-graphics.md#evidence-and-reproduction), using
these output names (or new names on subsequent runs). The logical 1440×810
request and actual 1920×1080 validation remain separate; do not drop pixel checks.

## Correctness validation

The physics integration regression now exercises long/prefix-related entity IDs
in nonlexical native allocation order, directional enter/stay reports, entity
disable/re-enable, and collider removal with a single exit and no stale contacts.
Existing tests cover sleeping support changes, body-slot reuse, reporting changes,
collision, character movement, and deterministic replay. The contact-phase suite
compares full event data and order against a reference implementation for sorted,
unsorted, and alternating streams across repeated frames.

Five focused Debug tests passed: physics3d, physics-contact-phases3d,
runtime-profiler, and bgfx 2D/3D app hosts. Both physics suites also passed in
Release. `performance_3d_lab` and `physics_3d_galton_board` validate without
diagnostics. This is not a full test-suite or Android qualification.

Debug configuration initially failed because its cached Blast source override
pointed into a vanished pre-reboot `/tmp` directory. Clearing that one CMake
override restored the pinned dependency through the normal download/hash checks;
no dependency version or source configuration was changed.
