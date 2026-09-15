# 3D Animation

This example plays skeletal animations embedded in a standard glTF/GLB model
through the shared animation state machine. Use the buttons on the left to
switch between the model's named idle, walk, sprint, jump, and dance clips.

`AnimationPlayer3D` belongs beside a `MeshRenderer` that references a `Model3D` asset:

```json
"AnimationPlayer3D": {
  "clip_name": "Idle_Loop",
  "speed": 1.0,
  "loop": true,
  "playing": true
}
```

Embedded glTF animation names can be used directly. In this asset, `clip_0` is
`A_TPose`, so selecting it appears static by design. The example now starts
with `Idle_Loop` and exposes several visibly animated clips. Every imported
clip also has a deterministic `clip_N` alias, so unnamed clips remain
addressable without depending on renderer internals. `AnimationStateMachine`
states use `model_clip_name` and share the same transitions, parameters, and
Lua controls as 2D animation states.

The project declares a 16.67 ms frame budget, 64 draw calls, and 32 resident
assets. These numbers are reference-scene limits, not engine-wide defaults.

## Character scaling probe

Press **C** (or the Character Scaling button) to open `scenes/crowd.scene.json`;
**B** returns. The camera, light, floor and HUD stay editable scene documents.
The `crowd` script component exposes `count` (default 64), `frozen`, and optional
`duration_seconds`. Increase the camera distance when manually increasing count.
The automated runner fits the camera for each population.

This reuses the existing licensed UAL1 character, with independent walk phases
and speeds. No new model downloads/copies are needed in the repository. The
frozen control keeps the same skin, poses, materials, draw path and placement;
it only stops playback. This isolates visual animation cost, **not** animated
AI, collisions, blend trees, root motion or gameplay input latency. Neither
mode disables collision in an existing gameplay scene. Pure animation has no
dynamic bodies; the shared floor now has an authored static collider.

```sh
SDL_VIDEO_DRIVER=x11 python3 scripts/benchmark_3d_visible.py \
  --binary build/linux-release/demi --output build/animation-crowd-run \
  --counts 16 64 --workloads animated frozen --vsync off \
  --seconds 12 --warmup-seconds 2 --width 1920 --height 1080
```

Run from the repository root, with a new output directory. X11 avoids the
fractional-scaling size changes observed on the reference Wayland desktop; it
does not change the Vulkan renderer. The runner retains raw per-frame CPU/GPU
timing, verifies the full crowd is visible and checks live/frozen mesh-rebuild
counts. Run captures sequentially and leave their windows untouched. A valid
capture is not automatically a frame-budget pass: inspect dropped simulation
time and frame tails too.

Run scene-transition/cleanup checks with:
`demi test linux examples/animation_3d --timeout 60`.

GPU skinning is automatic on Vulkan for this model; no new asset or component
is required. Add `--skinning gpu` to the benchmark to require the entire crowd
to use it, or `--skinning cpu` for the CPU reference. The runner records and
checks actual path populations rather than assuming the requested path worked.
From the project directory, `DEMI_GPU_SKINNING=0 demi run` forces CPU skinning.
See [GPU skinning support and measurements](../../docs/3d-gpu-skinning.md).

Add `--rig-layout split` to generate a temporary two-skin version of the same
character. Geometry and animation buffers stay unchanged. The benchmark uses
native asset reimport and validation before running it; the checked-in model is
never modified. This specifically checks multi-skin GPU eligibility rather than
substituting a simpler model.

For mixed simulation, use `--workloads mixed`. `count` then means total workload
objects: half animated characters with capsules and half colored box props, all
dynamic with collision reporting and sleeping disabled. Gravity and initial
lateral velocities are physical; collisions can stop their movement. The floor
and its collider are editable together in `crowd.scene.json`.

Use `--visual-rate 30 --visual-distance 30` to test opt-in distant-pose budgeting.
It holds far bone poses between refreshes, without slowing root motion, gameplay
events, input or physics. The Inspector also exposes `visual_rate`,
`visual_distance` and `mixed` on the crowd script. Use `--width 2560 --height 1440`
for 1440p; 1080p is the runner default. Mixed captures require the full active-body
population and contact pairs beyond floor-only contacts, not just visible meshes.

Current measurements and limits: [Milestone 2 high-resolution qualification](../../docs/3d-m2-high-resolution.md).
