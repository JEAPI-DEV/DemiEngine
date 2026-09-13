# Release scaling and Blast work boundaries

This continues Milestone 1 and makes one measured Milestone 2 improvement. It
does not qualify visible FPS, a production destruction scheduler, or a complete
2,000-entity game workload.

The next [physics optimization report](3d-physics-optimization.md) records the
sleeping fix and matched measurements for contact/synchronization changes.

## Method

Same Ryzen 7 6800H development machine as the [initial report](3d-milestone-1-baseline.md).
Linux Release (`-O3 -DNDEBUG`), GNU C++ 16.1.1, Jolt 5.6.0, fixed timestep 1/60 s,
720 headless frames, three repetitions per case. Collision, CCD, contact-reporting,
sleep, seed, and example settings were unchanged between sweeps. No builds or
other benchmark processes ran concurrently with these sweeps. Background desktop
activity, scheduling, and thermal variation were not controlled in a dedicated
lab, so small differences must not be presented as established improvements.

```sh
cmake --preset linux-release -DDEMI_BUILD_3D_FEASIBILITY=ON
cmake --build --preset linux-release --target demi
python3 scripts/benchmark_3d_lab.py --binary build/linux-release/demi \
  --output build/performance-3d-release-after --repeats 3 --frames 720
```

The first build reused the existing dependency source checkouts and host shaderc
through CMake overrides, but compiled the engine and runtime dependencies as
Release. The before/after report directories are
`build/performance-3d-release-before` and `build/performance-3d-release-after`.
Each retains its executable hash, machine metadata, raw scope CSVs, and logs.
Use a new output directory to repeat. The `before` binary predates `EntityLookup`.

## Results: median of three per-run p95 values (milliseconds)

| Population | Mesh before | Mesh after | Active rigid before | Active rigid after | Pile before | Pile after |
|---:|---:|---:|---:|---:|---:|---:|
| 250 | 0.264 | 0.192 | 0.750 | 0.728 | 1.212 | 1.232 |
| 500 | 0.739 | 0.357 | 1.463 | 1.470 | 2.785 | 2.822 |
| 1,000 | 2.518 | 0.757 | 2.967 | 3.032 | 7.058 | 7.294 |
| 2,000 | 6.357 | 1.554 | 6.507 | 6.618 | 17.118 | 18.177 |

The 2,000 moving-mesh CPU-frame p95 fell about 75.6% (4.1x lower). This is not
a draw/rendering benchmark: mesh mode measures normal Lua transform API calls
and runtime traversal without GPU work. Percentiles use the final 600 calls;
the first 120 once-per-frame calls are outside that window.

The physics controls did not improve. The 2,000-body pile result increased about
6.2%; that difference is recorded rather than hidden, and needs more controlled
repetition before assigning a cause. This change does not optimize the Jolt
solver or its synchronization/contact path. The pile is still above a 16.7 ms
CPU-frame target before rendering, and remains an explicit optimization target.

## Change and ownership

The original entity service performed a linear `findEntity` scan for each
transform call, producing quadratic lookup work when every entity moved. In the
2,000-mesh Release baseline, `Lua.fixed_update` p95 was 5.241 ms in the first run,
versus 6.357 ms for the CPU frame.

`EntityLookup` is now a thread-confined scene utility owned by each Lua host's
entity-service path. It stores ID-to-array-index mappings, never entity pointers
or component pointers. Every hit validates the current ID, size changes rebuild
the map, and misses remain uncached so same-size replacement/rename is observed.
Normal unique-ID scene semantics are preserved across reallocation, reorder,
replacement, destruction, and world reuse. Const world queries elsewhere do not
gain hidden mutable shared state. Global `findEntity` is unchanged.

The utility is tested independently; Lua integration regression cases exercise
create, component-changing replacement, and deletion through shared world
commands. Public Lua APIs, authored JSON, collision behavior, and simulation
timesteps are unchanged.

## Blast timing experiment

Standalone Release Blast 5.0.6, pinned source, two warmups and ten repetitions
for each combination: chain/grid, sub-threshold/full bond damage, and batches of
64 commands versus one full batch. Every case checks resulting groups and IDs.
The 400 measured rows are in `build/blast-feasibility/work-benchmark.csv`.

| Chunks | Max single apply call, batch 64 | Max single apply call, full batch | Max split call |
|---:|---:|---:|---:|
| 250 | 0.001503 | 0.009428 | 0.013786 |
| 500 | 0.006121 | 0.023133 | 0.019126 |
| 1,000 | 0.005401 | 0.038983 | 0.040507 |
| 2,000 | 0.005500 | 0.138852 | 0.073719 |
| 4,096 | 0.010580 | 0.150134 | 0.155193 |

All values are milliseconds; each column is the maximum observed across that
population's sampled cases, not a mathematical worst-case bound. Split timing
excludes scratch allocation, result extraction, Jolt transitions, rendering, and
stress evaluation. No physical scene was created during these timed calls.

Damage command arrays provide a batching boundary. Splitting is still a
synchronous whole-actor operation; a scratch buffer is not a resumable iterator.
These results justify measuring/limiting asset complexity and budgeting damage,
split, and body-transition phases separately. They do not establish a universal
sub-millisecond destruction budget or demonstrate GPU acceleration is needed.
Actual topology replacement and stress workloads are needed before fixing the
production analysis/transition/queue-latency budgets in the roadmap.

Asset preparation itself reached 6.978 ms at 4,096 chunks in this sweep. It is
outside the damage/split figures above and belongs in cooking/loading, not in
the response to a hammer hit. The probe does not yet implement that cooked path.

## Validation

- 14 focused Debug CTests passed, covering Lua services, entity lookup, object
  mutation, scene/prefab lifecycle, watched reload, physics, profiler, the lab,
  both 2D/3D Galton replays, and Blast probes.
- Nine focused Release CTests passed; lookup mutations additionally passed a
  standalone address/undefined-behavior sanitizer run.
- A 60-frame Release Vulkan smoke run succeeded at the default 960x540 window
  with 250 bodies. Final counters reported 251 visible meshes, no culled meshes,
  and two batches. This overlapped correctness checks, has no GPU timestamp
  breakdown, and includes presentation pacing; it is functional validation only.
- `git diff --check` passed. The complete engine suite and physical Android
  execution were not run in this slice.

## Remaining work

- Visible GPU/frame-pacing and non-NVIDIA rendering measurements.
- Physics synchronization/contact and dense-pile profiling, with controlled runs.
- Compound-body transitions, mass/motion conservation, and topology versioning.
- Cross-frame queueing, coalescing, fairness, invalidation, and cancellation tests.
- Imported models, animation, streaming, and representative tower workloads.
