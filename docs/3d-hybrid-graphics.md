# Hybrid graphics qualification — 2026-09-13

## Selection and test conditions

After the user enabled hybrid graphics, Demi selected the RTX 3070 Ti Laptop GPU
(`10de:24a0`) in three unmodified default Vulkan launches. The pinned bgfx Vulkan
device-selection code prefers a discrete GPU and uses an integrated GPU as a
fallback. AMD driving the desktop does not make it Demi's default GPU; other
applications can have different selection policies.

Explicit selection through `VK_DRIVER_FILES` successfully rendered on both GPUs:

- Radeon 680M: `/usr/share/vulkan/icd.d/radeon_icd.json`, PCI `1002:1681`.
- RTX 3070 Ti Laptop: `/usr/share/vulkan/icd.d/nvidia_icd.json`, PCI `10de:24a0`.

These are per-process overrides, not system configuration changes. For example:

```bash
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json ./build/linux-release/demi run --project examples/performance_3d_lab/demi.project.json
```

The Ryzen 7 6800H laptop was on AC power; CPU governor reported `powersave`.
No power policy, firmware, or display setting was changed. The laptop panel runs
at 165 Hz with 133% scaling; the external monitor currently runs near 60 Hz at
100% scaling. The user confirmed that initial window placement followed the cursor
and that they moved early test windows. Those exploratory captures are excluded
from the controlled comparison below.

The user then left the laptop untouched. Radeon ran first, NVIDIA second, using
the same Release binary, laptop panel, and lab fixtures. All 24 controlled captures
verified 1920×1080 backbuffers, 165 Hz, the intended GPU IDs, normal deltas, GPU
timestamps, and no unfocused, minimized, or interrupted measured frames. The tests
did not disable background services or control temperature/clocks, so this is a
runtime comparison on this machine, not an isolated hardware benchmark.

## Results

Each workload used 2,000 bodies, three repetitions per VSync setting, a 12-second
run and two-second warmup exclusion. The primitive lab keeps collision enabled;
the rigid workload keeps all bodies active, while the pile permits natural sleep.

Values below are **medians of the three per-run p95 measurements**, in ms.
Physics is work per rendered frame, including zero on frames without a fixed step.
Overlapping timings and percentile columns must not be added together.

| GPU | Workload | VSync | Frame interval | CPU update | Physics/frame | Render preparation | GPU |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| Radeon | Moving bodies | On | 9.597 | 5.811 | 4.352 | 2.209 | 2.107 |
| Radeon | Moving bodies | Off | 9.989 | 5.934 | 4.394 | 2.205 | 2.102 |
| Radeon | Pile | On | 14.288 | 12.025 | 10.651 | 2.150 | 2.161 |
| Radeon | Pile | Off | 15.354 | 12.642 | 10.947 | 2.626 | 2.156 |
| NVIDIA | Moving bodies | On | 9.446 | 5.704 | 4.231 | 2.096 | 0.401 |
| NVIDIA | Moving bodies | Off | 9.204 | 5.787 | 4.283 | 2.257 | 0.403 |
| NVIDIA | Pile | On | 15.608 | 12.700 | 10.917 | 2.343 | 0.442 |
| NVIDIA | Pile | Off | 15.644 | 12.747 | 10.868 | 2.673 | 0.480 |

No controlled run discarded fixed-step simulation time after warmup. Recorded
warmup-excluded wall-clamp totals were below 0.0001 ms per GPU batch (numerical
noise). This does not assert zero startup lost time.

Worst per-run frame p95/p99 were **17.037/20.126 ms on Radeon** and
**15.757/17.547 ms on NVIDIA**. The Radeon `pile-2000-off-2` run narrowly misses
the provisional p95 ≤16.7 ms gate; the other Radeon runs and all NVIDIA runs meet
it. Both meet p99 ≤25 ms. Individual frame maxima were 31.977 ms and 35.568 ms,
respectively: passing percentile budgets does not mean every frame meets 60 FPS.

The measurement-validity flag is not a performance-pass flag. Radeon compatibility
and the requested short benchmark sweep are verified, but a clean all-runs Radeon
performance gate remained open at this baseline. The subsequent
[physics-overhead optimization and rerun](3d-physics-overhead.md) meets the
provisional p95/p99 gate on both GPUs; longer-duration and game-content gates
remain separate.

## Interpretation and next checks

GPU execution is about 2.1 ms on Radeon versus 0.4–0.5 ms on NVIDIA, yet frame
times are similar. The pile's measured CPU/physics cost dominates GPU work; moving
body tests also exhibit presentation/backend waiting. Optimizing shaders alone is
unlikely to materially improve this primitive lab's worst frames. VSync-off runs
are not guaranteed to escape compositor pacing.

Do not attribute small CPU differences solely to the selected GPU or claim Radeon
is faster from one median. Shared power/thermal conditions and scheduling were not
isolated. The older dedicated-only captures used a different display refresh rate
(144 Hz), so they are not a controlled before/after comparison with these results.

Next: investigate remaining pile CPU frame-time variability, then qualify a longer
thermal soak, deliberate resize/monitor-transfer/minimize/restore cycles, and
representative imported/animated/material-heavy scenes. Early window transfers did
complete, but are not a systematic surface-lifecycle test. Android and other iGPUs
remain untested. This does not qualify a full 2,000-character game or destruction.

## Evidence and reproduction

Raw reports, frame CSVs, logs, and binary hashes are retained locally under
`build/hybrid-controlled-radeon/` and `build/hybrid-controlled-nvidia/`.
Default-selection probes are in `build/hybrid-default-selection/`. Earlier
`build/hybrid-radeon-*` pilots are not inputs to the comparison above.

The SDL window request is logical size, not drawable pixels. At this panel's
133% scale, 1440×810 logical produces 1920×1080 pixels. The runner's existing
`requested_pixels` metadata consequently contains `[1440, 810]`; the trace and
summary `widths`/`heights` establish the actual pixel size. This session used an
in-memory summary wrapper to validate actual 1080p, without changing source or
relaxing dimension checks:

```python
import os, sys
import scripts.benchmark_3d_visible as bench

original = bench.summarize
bench.summarize = lambda trace, warmup, width, height: original(trace, warmup, 1920, 1080)
for gpu, driver in [('radeon', 'radeon_icd.json'), ('nvidia', 'nvidia_icd.json')]:
    os.environ['VK_DRIVER_FILES'] = '/usr/share/vulkan/icd.d/' + driver
    sys.argv = ['benchmark', '--binary', 'build/linux-release/demi',
                '--output', 'build/hybrid-controlled-' + gpu, '--counts', '2000',
                '--seconds', '12', '--warmup-seconds', '2', '--repeats', '3',
                '--width', '1440', '--height', '810']
    bench.main()
```

Run from the repository root with fresh output directory names. Keep the cursor
on the laptop panel before launching and leave windows unchanged throughout.
For other scaling factors, adapt the logical request and independently verify
drawable dimensions. No engine code or authored example was modified for testing.
