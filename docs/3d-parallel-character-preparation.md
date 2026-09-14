# Parallel character preparation

2026-09-14, Milestone 2. This improves the existing CPU skinning path; it does not
implement GPU skinning, animation LOD or a new character-count guarantee.

## Evidence and implementation

The 8,546-vertex / 65-bone UAL1 crowd still spent most animated-mesh preparation
time in normal reconstruction. A diagnostic build split normal generation,
packing and buffer update: across its run, normal reconstruction accounted for
4.93 seconds of the 5.78 seconds labelled `skin_upload_cpu`, while buffer updates
accounted for 0.33 seconds. Those are aggregate scope durations, not per-frame
latencies. The temporary diagnostic capture is retained in
`build/characters-sep14-detail/`; the final path exposes the new boundaries below.

The renderer now prepares visible, changed character meshes in CPU-only batches:

- `BgfxRenderer3DAnimation.cpp` owns pose-cache checks, batch construction, worker
  dispatch, error collection and ordered GPU uploads. Workers read immutable
  model/scene inputs and write separate outputs; they never access Lua, mutate
  the world, submit graphics commands or touch the shared profiler.
- `MeshVertexPreparation3D` owns the validated normal/vertex algorithm shared by
  ordinary mesh upload and animated workers. It preserves the previous triangle
  accumulation order, normalization, supplied normals, UVs and packed colors.
- `GpuMesh3D::uploadPrepared` owns graphics resources and consumes prepared CPU
  vertices on the render thread. Changing the source model clears the old GPU
  topology before upload. Dynamic in-place updates require unchanged topology.

The existing worker pool is reused (eight workers on the reference machine, with
caller participation), rather than spawning another pool. At most 16 characters
and 262,144 vertices are staged per batch. An individually larger model runs
alone without decimation. This caps packed vertex output at 9 MiB for normal
batches, plus per-task pose/position/normal scratch; it is not a total RSS limit.

No animation updates, vertices, lighting, normal reconstruction or collisions
were removed. Frozen poses remain cached. Model reload invalidates sampled
geometry; loop policy and world transforms for procedural bone targets now
participate in pose-cache invalidation. Preparation precedes opening the primitive
canvas, allowing a corrected pose to render after a failed preparation attempt.
`parallelFor` also joins already-submitted jobs if dispatch throws, before the
borrowed caller state can unwind.

## Reproduction and results

Ryzen 7 6800H, NVIDIA RTX 3070 Ti Laptop (vendor 0x10de/device 0x24a0), optimized
Linux build, SDL X11 / bgfx Vulkan, 1280×720, VSync off. Two sequential eight-second
runs per 16/64-character case, with two seconds excluded for warmup. No builds
or independent benchmark processes overlapped the captures.

```sh
SDL_VIDEO_DRIVER=x11 python3 scripts/benchmark_3d_visible.py \
  --binary build/linux-release/demi --output build/characters-repeat \
  --counts 16 64 --workloads animated --vsync off --seconds 8 \
  --warmup-seconds 2 --repeats 2 --width 1280 --height 720
```

Ranges below are the two runs' p95 values, milliseconds, not confidence intervals.

| Characters | Frame interval before | Frame interval after | Render CPU before | Render CPU after |
|---:|---:|---:|---:|---:|
| 16 | 14.12–15.06 | 4.12–4.17 | 12.31–12.45 | 3.30–3.34 |
| 64 | 50.56–60.00 | 15.95–16.07 | 47.72–57.78 | 14.49–14.63 |

All matched captures passed resolution, visibility and playback checks. The new
runs discarded no fixed-step time during the measured window. The baseline has
large unexplained frame outliers, including a p99 above one second in one run;
do not attribute removal of those outliers solely to this change. Frequencies,
thermals and background desktop activity were not controlled. Startup clamping
and discarded time remain in the raw reports, not hidden by the warmup table.

A separate 64/128-character live/frozen control sweep also passed capture checks:
128 live characters had 28.75 ms frame p95 (30.20 ms p99); frozen 64/128 controls
were 0.51/0.61 ms p95 with no pose rebuilds after warmup. The 64-character matched
p99 values were 17.77/16.65 ms: this is progress toward 60 FPS, not a guarantee
that every frame meets 16.67 ms. GPU skinning remains valuable for higher counts.

A subsequent 25-second visual-review run on the final instrumented path measured
21.50 ms p95 (23.35 ms p99) with zero measured-window discarded fixed time. It
included screenshot capture and was not part of the matched comparison; the
cause of its slower tails was not isolated. This further rules out a sustained
60 FPS claim. The image is retained at `build/characters-sep14-review.png` and
the complete trace at `build/characters-sep14-review/`.

Raw before/after traces, logs and executable hashes are in
`build/characters-sep14-before/`, `build/characters-sep14-parallel/` and
`build/characters-sep14-controls/`. These are the same independent animation
workloads documented in [the example](../examples/animation_3d/README.md), not
physics-enabled characters or mixed gameplay.

## Reading the profiler

`Renderer3D.animation_prepare_wall` is the elapsed preparation latency, including
batch dispatch, waits and render-thread uploads. Existing `animation_rebuild`,
`skin_cpu` and `skin_upload_cpu` entries sum measured per-character task durations;
with concurrent workers, these durations overlap and can exceed the frame time.
Use them for work distribution, not as additive frame budgets. Their sums may
increase under contention even as frame latency improves.

`mesh_vertices_cpu` measures normal reconstruction/packing; `mesh_buffer_update_cpu`
measures the upload calls (including initial buffer creation when necessary).
Final batch gauges report the maximum staged mesh/vertex population per camera
and worker availability. Frozen frames produce no per-character rebuild samples.

## Qualification limits

Focused tests exercise the shared CPU geometry preparation, degenerate and supplied
normals, bad attributes/indices, worker/caller exception joining, multi-batch
rendering, frozen reuse, loop changes, bad procedural pose recovery and model
reload. Allocation-failure injection and sanitizer-based race/leak qualification
are not covered by those tests. A failed upload aborts rendering; this is not an
atomic multi-entity world transaction.

Four focused Release CTests passed, plus eight Debug CTests covering renderer,
GPU mesh preparation, skinning, job execution, mesh deformation, primitive canvas,
animation state machines and benchmark summaries. Desktop E2E passed repeated
crowd entry/exit and entity cleanup. The animation project validates for Linux
and Android; the procedural-spider project also validates. These do not replace
physical-device execution or a full-suite gate.

Sustained thermals, power use, Android execution, iGPU captures, richer characters
and full gameplay latency remain unqualified. No claim of a full repository test
pass or 1,000–2,000 animated characters follows from this slice.
