# Animated-character scaling: first measured slice

2026-09-13. Milestone 2 evidence; this does not implement GPU skinning or qualify
2,000 animated characters. Milestone 1's baseline/feasibility goal remains closed.

## Workload and boundary

`examples/animation_3d/scenes/crowd.scene.json` reuses the existing UAL1 Standard
model. One script populates a deterministic grid with independent walk phases and
slightly different playback speeds. The frozen control preserves those initial
poses, geometry, materials, placement and the per-character skinned draw path.
There are no crowd rigid bodies, AI, blend layers or root motion in this probe.
Its purpose is to isolate visual animation, not substitute for mixed gameplay.

The visible runner now supports `--workloads animated frozen`. It checks exact
drawable resolution, the full visible population and per-frame animation-rebuild
counts. Live captures require one rebuild per character per frame; frozen
captures require none after warmup. Existing GPU timestamp, interruption and
physics error checks still apply. Capture validity is not a frame-budget pass.

The renderer reports nested timings:

- `Renderer3D.animation_rebuild`: complete animated-mesh cache refresh.
- `Renderer3D.skin_cpu`: pose evaluation and vertex skinning.
- `Renderer3D.skin_upload_cpu`: normal reconstruction, vertex packing and upload
  submission. This is **CPU work**, not a GPU transfer measurement.

Use the per-frame CSV totals for crowd costs. Session scope percentiles sample
individual character calls. Do not add nested timings or independent percentiles.

## Measured bottleneck and change

The live/frozen difference is predominantly CPU animated-mesh preparation, not
the animation clock. Inspection found unchanged UV and packed vertex-color arrays
being allocated and rebuilt on every character update, despite already existing
in the model's cached rest geometry. Rendering now reuses that model-owned data
and topology. Asset reload replaces it together with the skin source; no new
per-character persistent cache or reduced animation quality is introduced.
Positions, normals and dynamic vertex uploads still update every live pose.

Profiling implementation now links through `demi-core`, allowing the renderer
and standalone renderer tests to use it without importing the application loop.
No public component or Lua contract changed.

## Preliminary results

Ryzen 7 6800H / NVIDIA device 0x24a0 (RTX 3070 Ti Laptop), Linux Release,
SDL X11 / bgfx Vulkan, 1280×720, VSync off. One eight-second run per case, first
two seconds excluded; all captures ran sequentially without concurrent builds.
CPU frequencies and temperatures were not controlled. These are short-run
observations, not repeated-run confidence intervals or sustained qualification.

| Characters | Animation rebuild p95 before → after | Frame interval p95 before → after |
|---:|---:|---:|
| 16 | 12.89 → 10.38 ms | 14.54 → 12.19 ms |
| 64 | 64.94 → 46.41 ms | 67.76 → 49.49 ms |

The 64-character case still fails a 60 FPS frame budget. Measured-window dropped
fixed time was 16.67 ms before and zero after; startup clamping/drops remain in
the raw report and are not hidden as a full-run qualification. Frozen controls
did not rebuild meshes and stayed below 0.52 ms frame-interval p95. A Back HUD
button was added between the initial and optimized captures, so the animation
scope is the cleaner comparison than whole-frame presentation.

Raw baseline reports: `build/animation-crowd-x11/`. Optimized reports:
`build/animation-crowd-cached-attributes/`. Both retain executable hashes and
per-frame traces. Earlier `animation-crowd-initial` failed because dynamically
spawned models had not been explicitly preloaded; the project now declares that
startup asset. `animation-crowd-baseline` captured fractional-scaling resolution
changes and was rejected. Its frame timings are not used in the table.

See the [example README](../examples/animation_3d/README.md) for commands and
editable properties. Keep the same GPU, resolution and populations when repeating.

## Validation and next step

Focused tests cover the renderer's first/unchanged/advanced-pose rebuild scopes,
existing skinning correctness, profiler behavior, matched workload configuration,
and rejecting incomplete/frozen live captures. Three assert-based C++ tests now
keep assertions enabled in Release: previously the skinning fixture skipped
required setup and crashed with `NDEBUG`, while other checks could silently vanish.
This does not repair or qualify every unrelated Release test in the repository.

The example validates for Linux and Android. Desktop E2E checks open the crowd,
verify the configured population and return to the single-character scene twice,
checking that crowd entities are released. Android execution of this new visual
workload, iGPU measurements and sustained/mixed-gameplay tests remain pending.

Executed: four focused Release CTests and five Debug CTests (the latter also
includes animation-state-machine coverage), plus the desktop E2E above. All
passed after repairing the Release assertion setup. This is not a full-suite
result. A separate visual-review capture at `build/animation-crowd-review.png`
confirmed the complete 8×8 crowd and readable HUD; its screenshot run is excluded
from the comparison table.

Next split pose evaluation from vertex deformation and implement a GPU skinning
path with a tested CPU reference, correct normals and explicit fallback for
unsupported cases. Keep gameplay animation events and collision-critical work
independent of any later distance-based visual update budget. Cache reuse alone
does not remove the dominant per-frame vertex reconstruction/upload cost.
