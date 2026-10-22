# Editor profiler and skipped-view qualification

2026-10-22, Linux Release, Vulkan. This records bounded editor tooling, not a
full external GPU debugger or synchronized CPU/GPU trace.

## Display and capture

Embedded Play retains 240 immutable completed frames, with at most 4,096 CPU
scope events per frame and an explicit omitted-event count. Inspection retains
shared snapshots; it does not copy all trace events or stop simulation. CPU
nesting comes from measured scope starts/ends. GPU pass timing uses the shared
backend freshness/frame-ID filter and remains explicitly delayed.

The default graph is game CPU work: disjoint Frame.update + Render.submit
measurements. Editor/backend events and their descendants are removed from the
CPU timeline; remaining timestamps are rebased without closing gaps. Raw captures
and CSV retain editor and presentation measurements. The graph is not total
elapsed frame time, and GPU durations are never added to CPU durations.

Separate automatic CPU/GPU/history axes use rounded bounds and a two-second
shrink delay. Details refresh at 4 Hz; raw history retains every frame. Missing
GPU samples do not shrink the GPU axis. Manual axis choices survive session reset.
Presentation and actual backend frame-advance scopes remain available in raw
measurements; their wall durations include waiting and are not CPU busy time.

## Reproduced stalls

Repeatable command (run one engine instance, with no build running):

```sh
./build/linux-release/demi-editor --project examples/minimal_3d --play \
  --profile-frames /tmp/editor-frames.csv --max-seconds 35
```

Both captures used native Vulkan with the initial 1680x945 editor window; these
are not 4K standalone game measurements. Excluding the first 300 frames:

| Measurement | Before ownership correction | After correction |
| --- | ---: | ---: |
| Warm frames | 4,088 | 4,522 |
| Mesh collection stalls over 100 ms | 3 | 0 |
| Maximum mesh collection | about 545 ms | 0.136 ms |
| Median game CPU update + submission | not summarized | 1.005 ms |
| P95 / P99 game CPU | not summarized | 1.315 / 1.605 ms |
| Median full frame interval | 6.945 ms | 6.936 ms |
| Median presentation wall duration | 4.822 ms | 4.812 ms |

Evidence captured in /tmp/demi-spikes.csv and /tmp/demi-spikes-fixed.csv (temporary
local artifacts, not source). The three large stalls followed skipped Game draws.
Inactive-terrain maintenance discarded ordinary material textures, forcing
synchronous image decoding/mipmap preparation on the next draw. Cleanup now
respects asset ownership. Active 3D view intent is also independent of whether a
resize/target-preparation frame draws successfully. Truly inactive terrain still
retires its own GPU/geometry resources; asset replacement still releases old
ordinary textures.

Deterministic renderer tests cover three skip/resume cycles without extra image
uploads, same-handle reuse, actual terrain patches and fields, inactive field
retirement, terrain rewarm, and ordinary asset replacement. Editor view-demand
tests distinguish active pending targets, hidden views and 2D/HUD transitions.

## Remaining timing limits

The follow-up capture still contains two physics wall-time outliers: 3.225 ms and
9.325 ms inside Jolt PhysicsSystem::Update. Of 1,975 warm physics samples, median
was 0.0733 ms, P95 0.1055 ms and P99 0.1317 ms. Both outliers had one fixed step,
six bodies, one active body, no contact pairs/events and no update errors. The
persistent Jolt worker pool/barrier is a scheduling candidate, not a demonstrated
cause. No speculative thread-count or allocator change was made. Desktop/backend
presentation variation also remains; this work does not claim zero frame jitter.

## Checks

Release editor/CLI builds and ten distinct focused suites passed across the
combined runs: runtime profiler, editor diagnostics/profiler, editor Play,
editor workspace, frame timing, editor render views, bgfx 3D renderer, glTF
materials, asset pipeline and model LOD generation. Latest game-only projection
and auto-scale regressions passed. Minimal 3D validates without diagnostics.
This is a focused gate, not a claim that the full repository test suite ran.
