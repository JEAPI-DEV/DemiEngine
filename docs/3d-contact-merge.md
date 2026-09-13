# Sorted contact-phase merge

This follows the [sleeping and physics integration fixes](3d-physics-optimization.md).
Only contact-phase assignment changed; Jolt stepping, collider shapes, contact
generation, reporting, sleep settings, and simulation timing are unchanged.

## Change

Rigid contacts are already emitted in canonical pair order. Their prior-frame
active contacts retain that order, although exits are appended separately.
`PhysicsContactPhases3D` now checks those ordering properties and merges the two
streams linearly, avoiding temporary hash sets in the common sorted case.

It assigns enter/stay in current dispatch order and appends exits in previous
dispatch order. Existing exits are ignored as prior active contacts. Reverse
recipient entries and duplicate pairs retain their original pair-level semantics.
The general hash-set implementation remains for unsorted inputs, including mixed
character-contact ordering. No new ordering requirement is imposed on callers.

The original current-contact count is retained during exit emission so newly
appended exits cannot accidentally participate in current-pair matching. Output
capacity is reserved before the merge or borrowed-key hash path.

## Matched Release measurements

Same development machine, 720 headless frames, fixed 1/60 s, 2,000 entities,
unchanged seed/collision settings, three repetitions per implementation. The
builds completed before measurement. Desktop/background activity was not removed.
Percentiles use the final 600 scope calls, not the entire session.

Before and after snapshots:

- `build/linux-release/demi-contact-phases-before`
- `build/linux-release/demi-contact-phases-after`

They were run in before/after/after/before/before/after order. Reports, logs and
executable hashes are retained in `build/contact-merge-{1..6}-{before|after}`.

Median of three per-run p95 values, milliseconds:

| Workload | CPU frame before | CPU frame after | Physics step before | Physics step after | Phase assignment before | Phase assignment after |
|---|---:|---:|---:|---:|---:|---:|
| 2,000 active bodies | 7.325 | 7.022 | 5.720 | 5.520 | 0.280 | 0.305 |
| 2,000-body pile | 16.787 | 15.913 | 14.133 | 13.860 | 2.887 | 2.318 |

Dense-pile phase assignment was about 20% lower and whole CPU-frame p95 about 5%
lower. The active workload's individual phase scope increased by 0.025 ms;
this is not a claim of improvement for every contact population. The modest
whole-frame differences remain subject to desktop scheduling variation.

Raw CPU frame p95 values:

- Active before: 7.325, 8.515, 7.018; after: 7.022, 6.689, 7.552.
- Pile before: 16.983, 16.787, 15.876; after: 15.913, 16.029, 14.452.

Do not add independent scope percentiles or compare this run directly with a
different load period as though it were the same A/B test. The pile's CPU-only
result is below 16.7 ms in this comparison, but does not qualify a rendered 60 FPS
game: draw submission, GPU cost, frame pacing, and different hardware still need
measurement.

## Regression coverage

The reference-algorithm test compares all contact payload fields and output order
for 0, 1, 80, and 4,000 pairs, in sorted, unsorted, and alternating input orders.
It covers forward/reverse recipient entries, empty frames, old exits, and changing
contact membership. Both paths preserve the original semantics.

Validation passed: 10 focused Debug CTests (including both Galton replays), seven
focused Release CTests, and a standalone address/undefined-behavior sanitizer
run of the phase tests. `git diff --check` is clean. The full engine suite and
GPU performance qualification were not run in this slice.

Reproduce current measurements with a fresh output directory:

```sh
python3 scripts/benchmark_3d_lab.py --binary build/linux-release/demi \
  --output build/contact-merge-current --counts 2000 --workloads rigid pile \
  --repeats 3 --frames 720
```
