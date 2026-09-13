# 3D performance lab

Press **D** or choose **Mesh Denting** for the native deformation probe:
click the right barrel or press Space to fire at it, M to change mass, V to change
speed, R to restore, B to return. Dent depth is calculated natively from actual
pre-solver contact energy. The engine refines the existing barrel mesh at runtime.
The left barrel shares its model but remains unchanged. This is visual damage;
only the right barrel opts in with `Dentable3D` (also available on prefabs).
Collision remains the original hull. See [mesh denting](../../docs/mesh-denting.md).

An executable scaling probe, not a visual showcase or a claim of 2,000-entity
support. The environment, camera, and lab settings are editable in
`scenes/main.scene.json`. Lua creates the measured population using normal
engine APIs; there is no engine-specific benchmark fast path.

The interactive scene opens with 250 falling barrels. The benchmark scripts
still default to `--geometry primitives` and explicitly override scene settings;
choose `--geometry barrel` when comparing the imported-model workload.

Edit the lab's script properties:

- `count`: 1–5,000; standard sweep 250, 500, 1,000, 2,000. Extended stress
  sweep: 2,000, 3,000, 5,000. The upper bound is a test size, not a performance claim.
- `workload`: `mesh` moves visual primitives with `Transform3D.set_position`;
  `rigid` creates gravity-free moving bodies with collisions enabled and sleeping
  disabled; `pile` drops bodies under gravity and permits sleeping.
- `varied`: alternate spheres/cubes and eight colors, instead of one shared
  primitive/color. This is not yet an imported-model/material diversity test.
- `geometry`: `primitives` retains the sphere/cube baseline; `barrel` uses the
  imported Blender steel barrel and its shared collider asset. In barrel mode,
  `varied` changes tint only, not shape. `mesh` mode renders barrels without
  physics; rigid/pile modes use the convex collider.
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
The default benchmark geometry is spheres/cubes. Add `--geometry barrel` to
either runner for the barrel variant; metadata records the choice. The original
Blender model is in `assets/models/barrel/barrel.blend`, and the collider is in
`assets/colliders/barrel/barrel.collider.json`. Scripts attach it through
`ModelCollider3D = { asset = "asset://colliders/barrel" }`; no prefab or point
array is required in the spawning script. The optional prefab references the
same collider file. The project explicitly preloads the model and collider;
it does not load every project asset automatically.

Barrels are 0.70 m across and 0.95 m tall, versus the baseline's 0.70 m sphere
diameter. Spawn spacing and camera remain unchanged. Barrel piles receive small
deterministic initial tilts to exercise tipping/rolling; no forces or positional
corrections are applied afterward. Consequently, this compares useful workloads,
not identical physics problems. No model LOD is applied during the stress test.

```sh
python3 scripts/benchmark_3d_visible.py --binary build/linux-release/demi \
  --output build/barrel-test --geometry barrel --counts 2000 5000 \
  --workloads rigid pile --vsync on --seconds 12 --warmup-seconds 2 --repeats 3
./build/linux-release/demi test linux --project examples/performance_3d_lab
```

The desktop E2E test checks a directly created asset-backed barrel falling and resting upright and
on its side at the heights implied by its convex proxy, plus ray hits at its
center and misses outside its round hull. Run it visibly: the
ordinary headless launcher has a short default frame limit unsuitable for its
waits. Geometry, imported-asset metadata, and shader appearance can also be
inspected with the ordinary asset tools and the editable Blender source.

## Tower impact test

Run the project and press **T**, or click **Tower Test**. In the tower scene:

- **Space / Fire:** launch a heavy physical sphere at the base.
- **R / Reset:** rebuild the tower.
- **B / Lab:** return to the population benchmark scene.

`scenes/tower.scene.json` owns the camera, light, floor, and editable script
properties: columns, levels, projectile mass, and projectile speed. The default
is 8 × 8 × 16 = 1,024 barrels. Larger configurations need camera adjustment and
their own performance checks. The launcher is fixed at the base; this is a
collision test, not a first-person weapon/controller example.

Every barrel is a dynamic rigidbody under gravity. The projectile uses a sphere
collider, mass 60, initial speed 35 m/s, and continuous collision. The only timed
cleanup removes spent projectiles after eight seconds; barrels are not deleted,
teleported, frozen, or given scripted collapse impulses. Reset cancels old shot
timers so they cannot remove a new scene's projectile.

Tall stacks need higher solver quality: tower barrels explicitly use
`solver_velocity_steps: 64` and `solver_position_steps: 16`, with moderate
damping and friction. These are iterations inside each fixed step, not a slower
simulation rate. Zero is the engine-wide default for both fields and retains
the backend's default settings. Higher settings affect the connected contact
island and cost more CPU; the ordinary population tests do not enable them.

The initial 256-barrel headless stability check kept every top barrel supported
for 30 simulated seconds with the tower settings; the default-quality control
did not. The larger default passed repeated visible impact tests on both GPUs
(see `docs/3d-1024-tower-qualification.md` at the repository root).
The desktop E2E test checks all tagged top barrels before the shot, then checks
that upper barrels fall, reset works, and shared collider assets survive scene
transitions. This is not a qualification of a 5,000-barrel tower.

`scripts/benchmark_barrel_tower.py` qualifies standing, full-collapse, and
active-burst timings separately. It uses the real test/UI path to fire, verifies
pre-shot support and post-shot falling, and checks simulation time and native
capacity errors. Its `--velocity-steps` / `--position-steps` options override only
the temporary fixture. The native defaults remain unchanged.

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
