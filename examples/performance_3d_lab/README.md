# 3D performance lab

An executable scaling probe, not a visual showcase or a claim of 2,000-entity
support. The environment, camera, and lab settings are editable in
`scenes/main.scene.json`. Lua creates the measured population using normal
engine APIs; there is no engine-specific benchmark fast path.

Edit the lab's script properties:

- `count`: 1–5,000; standard sweep 250, 500, 1,000, 2,000. Extended stress
  sweep: 2,000, 3,000, 5,000. The upper bound is a test size, not a performance claim.
- `workload`: `mesh` moves visual primitives with `Transform3D.set_position`;
  `rigid` creates gravity-free moving bodies with collisions enabled and sleeping
  disabled; `pile` drops bodies under gravity and permits sleeping.
- `varied`: alternate spheres/cubes and eight colors, instead of one shared
  primitive/color. This is not yet an imported-model/material diversity test.
- `duration_seconds`: optional auto-quit using the normal game timer API. Zero
  leaves the lab running until its window is closed.

The rigid case starts all bodies moving, but collisions can stop them; the
active-body gauge is not a moving-body counter. Piles must be reported separately
from active-body workloads. CCD is off, contact reporting remains on, and the
fixed timestep is 1/60 s. Mesh mode includes normal Lua-to-engine transform calls.
The fixed camera observes the test region; no moving-camera/streaming probe or
automated all-visible qualification has been implemented yet.

## Visible 1080p capture

```sh
python3 scripts/benchmark_3d_visible.py --binary build/linux-release/demi \
  --output build/visible-3d --counts 2000 --workloads rigid pile \
  --vsync on off --seconds 12 --warmup-seconds 2 --repeats 3
```

This runner uses normal wall-clock deltas, removing headless/fixed-delta overrides
from the child environment. It requests 1920x1080 and checks actual backbuffer
pixels, GPU timing availability, minimization/interruption, and run duration.
It also requires `Physics3D.update_error_steps` telemetry and rejects any capture
with a physics update error, including errors during startup. This cumulative
per-physics-world counter exposes Jolt capacity failures that can omit contacts.
Old binaries without the counter cannot qualify through the current runner.
Native contact capacity is now 32,768 per world (previously 10,240, which
overflowed in the 5,000-body pile). This raises memory requirements, does not
guarantee capacity for arbitrary arrangements, and does not lower solver quality.
It saves raw per-frame traces, aggregate reports, machine/executable identity and
per-run JSON summaries. No authored example files are modified.

Runs last the requested game-timer duration rather than a fixed rendered-frame
count, so different refresh/throughput rates can be compared over similar
simulation periods. Fixed-step drops and delta clamping are reported separately.
The first two seconds are excluded by default; delayed GPU samples from before
the warmup boundary are excluded too. Physics frames with zero steps contribute
zero work; missing GPU samples remain missing. The maximum-frame limit is only
a safety bound. Closing a window early makes a short capture invalid.

VSync-off does not guarantee unrestricted presentation on a composited desktop.
Report the measured cadence, GPU identity and refresh rate rather than assuming
VSync means 60 Hz. `valid_capture` describes usable evidence, not a performance
pass: inspect frame intervals, lost time, visible population and GPU costs.

For the larger stress sweep, use `--counts 2000 3000 5000`. Keep the same camera,
resolution, refresh rate, and background applications across counts. A smooth
video or acceptable render FPS does not qualify a run that drops simulation time.
These are spheres/cubes, not a recreation of the Crysis barrel model or scene.

```sh
./build/linux-debug/demi validate examples/performance_3d_lab
./build/linux-debug/demi run --project examples/performance_3d_lab/demi.project.json
python3 scripts/benchmark_3d_lab.py --binary build/linux-debug/demi \
  --output build/performance-3d-debug
```

The runner uses temporary project copies and a new output directory, leaving
authored files untouched. It saves machine/build metadata, individual logs,
scope CSVs, and a summary. Default runs use 720 frames and three repetitions;
CSV percentiles retain only the final 600 calls per scope, excluding the first
120 frames for once-per-frame scopes. Session maxima still include startup.
The executable hash identifies the exact measured build. Stop competing builds
and heavy workloads before collecting qualification results.

These are **headless CPU timings**, not FPS or GPU measurements. Headless
simulation advances one fixed step per frame regardless of elapsed wall time.
Use an optimized build and visible GPU profiling before evaluating the roadmap's
1080p/60 FPS objective. Animated characters, imported models, streaming, dense
collision bursts, and destruction workloads remain to be added.
