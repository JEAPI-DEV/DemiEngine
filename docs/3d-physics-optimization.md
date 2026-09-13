# 3D physics: sleeping correctness and integration cost

This change fixes incorrect sleep/wake synchronization and reduces the engine
work around Jolt. It does not replace Jolt, lower solver quality, disable
collision/reporting, change collider shapes, or slow the fixed timestep.

The subsequent [sorted contact-phase merge](3d-contact-merge.md) further reduces
phase-assignment work for ordered contact streams; its measurements are separate.

## Confirmed bug

Previously, every awake component caused `BodyInterface::ActivateBody` on every
step. Jolt 5.6.0's implementation resets the sleep timer even if the body is
already active. Consequently, otherwise resting bodies never naturally slept.
A new regression test failed on the original code and passes on the corrected
integration. Replaying the stale component state could also undo a velocity
command that had just woken a sleeping backend body.

Body records now distinguish the last published velocity/angular velocity/awake
state from new authored edits. Unchanged values are not replayed as commands.
Explicit API wake calls remain commands; force/impulse, shape, and gravity edits
retain their physical effects. The test checks both natural sleeping and
`allow_sleep=false`, not a timer-based freezing shortcut.

## Collision and contact preservation

Jolt removes its simulation contacts when bodies sleep. Demi now defers removal
processing until the solver has joined, checks whether the body pair still has
contacts, and retains valid resting pairs. Cached pairs use native body IDs with
sequence numbers, not entity pointers. Support movement, removal, disable, and
shape edits wake nearby bodies using a broadphase bounds query with contact
tolerance. Resting stacks therefore fall when their support is removed.

Per-step `BodyFrame` snapshots store layer/reporting metadata once per body.
Native-slot lookup checks both the body generation and current-step epoch, so a
removed/reused native slot cannot resolve to an old entity. Worker callbacks only
read the completed snapshot. Active-state publication happens after they join.

Contact phase reconstruction now lives in `PhysicsContactPhases3D` independently
of Jolt. It swaps the current/previous buffers, uses borrowed pair keys instead
of repeatedly allocating concatenated ID strings, and bulk-allocates temporary
set nodes through a standard-library monotonic resource. Output capacity is
reserved before creating string views to preserve short-string lifetimes.
Regression tests compare event ordering, enter/stay/exit, and payload fields
against the previous phase algorithm.

Other integration changes: body state is read under one native read lock rather
than several independent locks; root transforms return their already-world-space
values without allocating a cycle set or composing unused quaternions. Parented
transform resolution and cycle validation retain their existing paths.

## Measurement method and noisy intermediate results

Same Ryzen 7 6800H machine and Release configuration as
[the previous scaling report](3d-release-scaling.md). Every run used 720 headless
frames, fixed 1/60 s, the existing lab's seed and scenes, CCD off, and contact
reporting on. Percentiles use the final 600 scope calls. No builds ran during
timing runs; desktop/background load was not eliminated or modified.

The first before sweep (`build/physics-3d-before`) gave median whole-frame p95
values of 6.398 ms for 2,000 active bodies and 16.990 ms for the pile. Intermediate
candidates did not reliably improve these numbers: `physics-3d-after`,
`physics-3d-contact-detail`, `physics-3d-frame-snapshot`, and
`physics-3d-dense-snapshot` retain the unsuccessful/noisy runs. Untouched 2D and
other scopes also changed substantially as background load varied.

To avoid comparing different load periods as though they were a clean A/B test,
separate before/after executables were run interleaved. The baseline was rebuilt
with the pre-change `PhysicsWorld3D.cpp` and `Transform3DHierarchy.cpp`, retaining
the profiler gauges and all other existing engine changes. Candidate source was
restored immediately afterward, and the normal Release executable rebuilt.
Snapshots remain under the ignored Release build directory:

- `build/linux-release/demi-physics-baseline`
- `build/linux-release/demi-physics-candidate`

Run order was baseline/candidate/candidate/baseline/baseline/candidate. Each ran
the 2,000-body active and pile cases once. Logs, executable hashes and full scope
CSVs are in `build/physics-paired-{1..6}-{baseline|candidate}`. These are three
runs per implementation, not a claim of exhaustive hardware qualification.

## Matched results

Median of the three per-run p95 values, milliseconds:

| Workload | CPU frame before | CPU frame after | Physics step before | Physics step after | Contact phases before | Contact phases after |
|---|---:|---:|---:|---:|---:|---:|
| 2,000 active bodies | 9.470 | 7.243 | 7.884 | 5.411 | 2.112 | 0.574 |
| 2,000-body pile | 24.017 | 17.194 | 21.613 | 14.495 | 11.201 | 5.319 |

Physics-step p95 fell approximately 31% and 33%, respectively. Whole CPU-frame
p95 fell approximately 24% and 28%. Independent scope percentiles must not be
added as though they occurred in the same frame.

Raw whole-frame p95 values:

- Active before: 9.470, 9.039, 9.811; after: 7.243, 6.869, 7.306.
- Pile before: 23.655, 24.498, 24.017; after: 16.125, 17.194, 17.640.

Both 2,000-body cases end with 2,000 active bodies in the matched runs. The active
workload explicitly disables sleep, so its gain is not a sleeping shortcut.
Pile mode allows natural sleep; its final active count does not prove that no
body slept earlier. The smaller 500-body pile now reaches zero active bodies,
while retaining physical collision and contact state. Sleeping is reported, not
concealed.

The dense pile still exceeds a 16.7 ms whole-frame budget before rendering under
this load. This is progress, not a declaration that a 2,000-body rendered game
meets 60 FPS. GPU/frame-pacing qualification remains outstanding.

## Validation and next work

- Physics regressions cover natural sleep, always-awake bodies, explicit wake,
  sleeping velocity/impulse commands, authored edits, removed/disabled/moved/
  reshaped supports, reporting re-enable, shape changes while asleep, gravity
  changes, and native-slot reuse.
- The focused Debug suite passed, including contact phases, hierarchy, Lua,
  prefab lifecycle, the performance lab, both Galton-board replays, and Blast/Jolt.
- Contact-phase tests passed a standalone address/undefined-behavior sanitizer
  run. This does not substitute for a full physics-backend sanitizer run.
- Follow-up targets: active contact generation/solver costs and remaining frame
  traversal, with controlled performance runs and visible GPU measurements.

Reproduce a current candidate sweep with a fresh output directory:

```sh
python3 scripts/benchmark_3d_lab.py --binary build/linux-release/demi \
  --output build/physics-current --counts 500 2000 --workloads rigid pile \
  --repeats 3 --frames 720
```
