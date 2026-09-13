# Milestone 1: initial 3D evidence

Status: first implementation slice, not performance qualification or a shipped
destruction feature. No optimization claim is made by these measurements.

For the subsequent optimized-build comparisons, entity lookup improvement, and
Blast work-unit measurements, see [Release scaling](3d-release-scaling.md).

## Implemented boundaries

- `examples/performance_3d_lab`: editable environment and script settings;
  primitive mesh motion, gravity-free active bodies, and settling piles.
- `scripts/benchmark_3d_lab.py`: temporary project variants, reproducible counts,
  process timeout/failure handling, binary/machine metadata, and retained reports.
- Existing runtime profiler: CSV p50/p95/p99 and sample counts; existing physics
  wrapper: total/active rigid-body gauges. These do not change physics behavior.
- `tools/3d-feasibility`: opt-in, pinned Blast core and a narrow probe boundary
  returning chunk IDs. No Blast SDK types leak into gameplay or engine components.

## Initial machine and method

AMD Ryzen 7 6800H, 8 cores / 16 logical CPUs, approximately 30 GiB reported RAM,
Linux x86-64, GNU C++ 16.1.1. CPU boost was reported disabled. The laptop contains
both Radeon 680M and GeForce RTX 3070 Ti graphics, but neither GPU is measured by
these runs. This is a development machine, not yet an agreed qualification target.

Engine build: `linux-debug`, Jolt 5.6.0. One run per case, 720 fixed simulation
frames at 1/60 s, identical seed and primitive colors. CCD off; contact reporting
on. Pile mode allows sleep; active-rigid mode does not. Machine and executable
hash are retained in `build/performance-3d-initial/machine.json`; raw logs/CSVs
stay under that ignored report directory, not in authored assets.

```sh
python3 scripts/benchmark_3d_lab.py --binary build/linux-debug/demi \
  --output build/performance-3d-initial --counts 250 500 1000 2000 \
  --workloads mesh rigid pile --repeats 1 --frames 720
```

Use a new output directory to repeat. Reported values below are p95 CPU frame
milliseconds over the final 600 scope calls. Session maxima include startup;
these are neither full-run percentiles nor visible FPS.

| Entities | Moving mesh | Active rigid bodies | Pile |
|---:|---:|---:|---:|
| 250 | 2.428 | 7.352 | 10.799 |
| 500 | 4.909 | 12.893 | 22.122 |
| 1,000 | 17.569 | 29.620 | 54.008 |
| 2,000 | 49.289 | 51.586 | 116.975 |

These single Debug runs are diagnostic, not a performance release gate. The
first small run overlapped short validation tests. Repeat isolated optimized
runs before making quantitative improvement claims. The runner advances fixed
simulation time independently of elapsed time, so it cannot prove real-time pace.

## What the scope evidence says

- At 2,000 moving meshes, `Lua.fixed_update` p95 is 44.195 ms versus
  `Frame.total` p95 49.289 ms. The script issues one normal transform API call per
  entity. Investigate the call/lookup path before blaming rendering or Jolt.
- At 2,000 active bodies, p95 values are 15.256 ms for body synchronization,
  11.562 ms for component synchronization, 11.463 ms for contact phases, and
  10.127 ms for Jolt simulation. Over all 720 calls, the first three scopes total
  approximately 20.79 s, versus 6.90 s for Jolt simulation. Do not add independent
  percentile values as though they describe the same frame.
- The active-rigid cases finish with the expected 250/500/1,000/2,000 active bodies.
  Active does not imply every body's velocity remains nonzero after collisions.
- The mesh case still incurs 2D/3D subsystem traversal costs despite having no
  dynamic physics population. Whether to cache active sets or skip work must be
  decided with correctness tests and optimized-build measurements.

These observations support investigating Demi integration overhead first. They
do not prove a specific algorithm is responsible or compare Jolt against another
backend. No behavioral optimization was applied during this baseline slice.

## Blast feasibility result

Blast 5.0.6 at the pinned revision builds and passes its CPU split probe in Linux
Release and Debug. The explicit low-level/common source list has no PhysX or CUDA
target; standalone dynamic linkage contains only system C/C++/math libraries.
See the [dependency audit and commands](../tools/3d-feasibility/README.md).

The fixture validates sub-threshold damage, three separated chunk IDs, and 100
resource lifetimes. The engine-linked probe maps singleton chunks to existing
Jolt-backed box bodies and checks floor settling, mass-dependent impulse response, identity,
and 20 world lifetimes. This does not yet validate compound topology changes,
physical mass/inertia conservation, inherited angular motion, or an intact-body
replacement transaction. No production destruction schema/API has been added.

Android ARM64/API 23 cross-compilation succeeded with NDK 29.0.14206865 after
using upstream's `NV_C_EXPORT` override for Android C linkage. No adb device was
connected, so neither Android execution nor performance is qualified.

## Next work

1. Collect repeated optimized CPU baselines and visible rendering measurements;
   fix the reference hardware/resolution/quality profile.
2. Measure the hot Lua transform path and physics synchronization/contact paths
   at finer granularity before changing algorithms.
3. Extend the lab to imported assets, animation, camera traversal and streaming.
   Add GPU, cumulative dropped-time/backlog, contact and memory/allocation evidence.
4. Extend the Blast/Jolt spike to intact compound assemblies and real topology
   replacement with stable subshape mapping and mass/motion conservation tests.
5. Run the CPU probe on a physical Android device and qualify non-NVIDIA rendering.

The existing Galton board remains a regression probe. The roadmap's full first
milestone and 1080p/60 FPS objective remain open.

## Validation performed

- Focused Debug build and six CTests: physics, profiler, lab validation/smoke,
  standalone Blast, and Blast/Jolt handoff passed.
- Existing `demi-runtime-physics-3d-galton-board-replay` passed (30.08 s).
- Clean checksum-verified source download, standalone Release build, and split
  test passed without a pre-existing source checkout.
- Standalone Blast Debug build with address/leak sanitizers passed. This is not
  sanitizer coverage of the engine-linked handoff.
- Android ARM64 cross-compile passed; no physical-device run was possible.
- Full engine test suite, optimized engine baseline, and visible GPU tests were
  not run in this slice.
