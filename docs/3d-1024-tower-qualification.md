# 1,024-barrel tower qualification

The current target is a roughly 1,000-barrel tower at stable real-time 1080p
performance. Around 2,000 is a stretch requirement, not a prerequisite for this
milestone. The former 256-barrel fixture was a functional starting point, not
an engine limit.

## Result

The existing implementation passes the provisional 60 FPS timing budget on both
GPUs at **1,024 barrels (8 × 8 × 16)**. No engine optimization, collision
simplification, LOD reduction, or solver-quality reduction was needed for this
target. The interactive tower now defaults to that population.

Three Release runs per GPU, 1920×1080 actual pixels, 165 Hz laptop display,
VSync requested on. NVIDIA `10de:24a0`, Radeon `1002:1681`; all GPU IDs,
dimensions, refresh rates, and focus state were checked in the saved traces.
The fixed timestep remains 1/60 s, with at most two catch-up steps per frame.
Barrels retain their full 2,676-triangle model, 32-point convex collider,
contact reporting, natural sleeping, and solver overrides of 64/16.

Each run waits 12 seconds, verifies every top barrel is still supported, fires
through the real HUD button, and observes another 12 seconds. The first two
seconds are excluded from standing-phase timing, not from correctness checks.
The test window displays an automated-test warning; no manual input is needed.

Worst **per-run** percentiles across the three repetitions, milliseconds:

| GPU | Window | Frame p95 | Frame p99 |
| --- | --- | ---: | ---: |
| NVIDIA | Standing | 10.299 | 11.670 |
| NVIDIA | Full impact/collapse | 10.925 | 11.879 |
| NVIDIA | Active-collapse burst | 10.936 | 11.906 |
| Radeon | Standing | 11.749 | 11.958 |
| Radeon | Full impact/collapse | 11.787 | 12.011 |
| Radeon | Active-collapse burst | 11.783 | 12.007 |

All six runs had zero discarded fixed-step time after warmup, zero physics
capacity errors, and no material wall-time clamping. Every barrel and the floor
remained visible throughout; the projectile adds one temporary mesh/body. All
64 top-layer barrels fell after impact. Peak activity reached 1,025 rigid bodies,
including the projectile, rather than only a small subset of the tower.

Standing barrels naturally slept. To avoid letting those cheap frames hide a
collapse bottleneck, the runner reports both the full impact window and a
contiguous active-burst window: from projectile creation through the last frame
with at least half the population active, plus the following frame to account
for poll-to-poll interval timing. The latter lasted approximately 10–10.6 seconds
in these runs. It is not a claim that every body stayed awake for that whole time.

The acceptance checks are p95 ≤16.7 ms, p99 ≤25 ms, no discarded fixed time,
no material wall clamping, valid GPU telemetry, and no focus interruption.
The burst also requires at least one second of evidence and near-full population
activity. Those additional activity/duration checks were verified against every
saved result. Individual frame maxima across all phases were 20.260 ms on NVIDIA
and 18.485 ms on Radeon; this is a percentile-budget pass, not a promise that
every frame is below 16.7 ms. Startup loading and long thermal-soak behavior are
not qualified here. Background services and clocks were not isolated.

## Reproduction and changes

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
python3 scripts/benchmark_barrel_tower.py --binary build/linux-release/demi \
  --output build/tower-repeat-nvidia --columns 8 --levels 16 --repeats 3 \
  --window-width 1440 --window-height 810
```

Use the Radeon ICD for its run. The logical window request above compensates
for this laptop panel's 133% scale; the default expected backbuffer remains
1920×1080. On a 100%-scaled display omit the two window-size overrides. Keep
the cursor on the intended display before launch and leave the window untouched.
Output directories must be new. Normal imported asset outputs must exist;
reimport the barrel model and collider manifests first on a fresh checkout.

Evidence: `build/tower-1024-nvidia-qualified/` and
`build/tower-1024-radeon-qualified/`, with raw/phase frame traces, logs, summaries,
GPU IDs, solver settings, dimensions, and binary hashes. An earlier setup-only
attempt omitted required generated import outputs and produced no capture; it
is not part of these results.

The changes stay in the example and measurement tooling: a 1,024-barrel default,
editable solver-quality properties, tags for scale-independent top-barrel checks,
and a phase-aware runner reusing the existing timing summarizer. The desktop
E2E test now checks every tagged top barrel, collapse, reset, and scene return.
Seven summary/runner unit tests and project validation pass; the final desktop
E2E run passes both gameplay tests at the larger default.

Native cylinder assets and automatic collider fitting remain separate authoring
improvements. A 2,000-barrel impact tower has not been qualified by this work.
