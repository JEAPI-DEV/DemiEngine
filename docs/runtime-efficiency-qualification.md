# Shared runtime efficiency and readability, 2026-10-11

## Scope and ownership

This pass addresses repeated engine work before terrain LOD or additional art.
Three GPT-6.1 Sol agents reviewed rendering, physics and runtime lookup paths;
the primary agent reviewed their rewrites, requested further separation where
code had merely moved, integrated backend changes, and ran all builds and timing
comparisons. The component container and public mutation model are unchanged.

- `MeshResourceRetention3D` collects live mesh ownership once per updating
  renderFrame, reusing owned ID keys. It retains invisible geometry and only
  evicts GPU resources after successful frame submission.
- `PreparedMeshDraw3D` owns per-draw uniform backing values. Descriptors are filled
  only for actual direct/skinned submissions, using reusable storage. The input
  signature expresses the required eight common uniform slots.
- `MeshInstanceBatch3D` groups by GPU mesh identity, complete texture handles,
  exact tint/material float bits and unlit state. Groups preserve first-seen
  order. Transparent draws remain separate; no decimal string keys are built.
- `BgfxRenderer3DMeshes` owns geometry-cache resolution and mesh submission;
  `BgfxRenderer3D` coordinates views, effects and presentation. Touched control
  flow now uses explicit branches and named decisions.
- `BgfxDrawBindings` resolves native uniform/texture handles once per submission
  into reusable scratch. Validation precedes native binding application. Every
  draw still applies its uniforms; there is no unsafe persistent GPU-state cache.
- `PhysicsBodyInputs3D` borrows transform, body and collider inputs for one
  synchronization operation. Nonphysical entities exit early. Signatures and
  body placement reuse the inputs and resolved transform. `JoltBodyShape3D`
  owns backend construction, keeping SDK types out of the native input view.
- Animation/camera/sprite paths reuse local component pointers and avoid queries
  for irrelevant entities. Sprite sorting compares referenced fields rather
  than copying strings. Material and texture libraries use the shared transparent
  `StringHash` with owning keys and allocation-free string-view lookup.

No scene density, resolution, material quality, physics cadence or shadow setting
was reduced. No persistent entity/component pointer index was introduced:
public native mutation and vector relocation still require live validation.

## Correctness fixes found during the rewrite

Static collision previously ignored parent-only pose changes because it compared
only local transform values. Synchronization now tracks authored world pose as
well. Parented dynamic bodies retain native motion between authoring changes;
unparented simulation publication updates its comparison baseline.

Sphere, capsule and convex collider offsets were absent from shape signatures.
Direct offset edits now rebuild collision geometry, as box offsets already did.

Additional texture validation now completes before applying the primary texture
binding, avoiding partial encoder-state mutation on rejected submissions.

## Controlled Release measurements

Baseline: commit `a6d15265`, preserved executable
`build/linux-release/demi-runtime-before-rewrite`. Both baseline and rewritten
executables use the same `linux-release` configuration. This is not the earlier
Debug-versus-Release comparison.

Workload: the unchanged cooked branching terrain project at
`/tmp/demi-terrain-material-cooked`, fixed scene camera, requested 2953 x 1743
window (reported backbuffer 2954 x 1744), four shadow cascades and 4x MSAA.
Every measured run reported 73,130 main-view triangles, 383 main-view batches
and 706 shadow batches. Builds and other benchmarks were stopped during timing.

Visible runs recorded 360 frames; fixed-step headless runs recorded 500. Analysis
excludes the first ten frames. A preserved baseline executable was rerun after
the rewritten executable to check clock/background variation.

| Warm-frame measure | Baseline | Final rewrite |
| --- | ---: | ---: |
| CPU render preparation, median | 5.591 ms | 3.915 ms |
| Visible frame work, median | 7.511 ms | 6.804 ms |
| Visible frame work, p95 | 9.651 ms | 7.557 ms |
| Fixed-step headless update, median | 2.419 ms | 1.924 ms |
| Fixed-step physics synchronization, median | 1.047 ms | 0.631 ms |
| Fixed-step physics synchronization, p95 | 1.136 ms | 0.705 ms |

Three baseline render-preparation medians ranged from 5.448 to 5.591 ms;
rewritten medians ranged from 3.834 to 4.048 ms. This supports roughly 28–30%
less CPU render preparation. Fixed-step synchronization fell about 40%, and the
whole headless update fell about 20%. The fixed-step comparison avoids attributing
fewer physics steps per faster rendered frame to cheaper physics execution.
GPU time stayed around 1.4 ms; these are CPU improvements, not a GPU-quality cut.

Reports and per-frame CSVs are under `/tmp/terrain-rewrite-{before,before2,control,
after,after2,final}*` and `/tmp/terrain-rewrite-*-headless*`. Reproduction:

```sh
SDL_VIDEODRIVER=x11 ./build/linux-release/demi run --project /tmp/demi-terrain-material-cooked/demi.project.json --window-size 2953x1743 --max-frames 360 --profile-report /tmp/rewrite.csv --profile-frames /tmp/rewrite-frames.csv
DEMI_HEADLESS=1 ./build/linux-release/demi run --project /tmp/demi-terrain-material-cooked/demi.project.json --max-frames 500 --profile-report /tmp/rewrite-headless.csv --profile-frames /tmp/rewrite-headless-frames.csv
```

## Native editor verification

A temporary copy of the source project rendered in the optimized Vulkan editor
at 5120 x 2806. Screenshot `/tmp/engine-rewrite-editor.png` was inspected; terrain,
materials and authored objects remained visible. The smoke exited normally.

The user's open editor was still using the replaced executable, confirmed by
Linux reporting `(deleted)` for its loaded executable. Restarting embedded Play
had not loaded the new native code. After relaunching the rebuilt editor, its
profiler at 6,250 frames reported:

- Update: 2.26 ms average, 3.39 ms p95.
- Render submission: 4.41 ms average, 5.03 ms p95.

The user's earlier screenshot showed 4.02/6.54 ms update and 6.91/8.15 ms render
submission (average/p95). Screenshot `/tmp/engine-rewrite-live-play.png` records
the updated session. Embedded GPU timing was unavailable; standalone GPU timing
is reported separately above. These UI measurements are not identical to the
standalone warm-frame medians.

Desktop focus initially prevented the relaunch: a shortcut reached VS Code
instead. Subsequent controls were verified visually after access was restored.
The user's saved terrain-name change was preserved and excluded from this patch.

## Regression gate

Nineteen focused Debug suites passed: physics3D, contact phases, collider assets,
compound colliders, destruction, runtime lifetime failure, render commands,
texture library, material library, deformation, bgfx 2D/3D renderers, sprite
animation, animation primitives, animation phase 7, visual animation budgeting,
camera2D, terrain world updates and terrain scatter runtime.

Coverage includes parent translation/rotation/scale/reparenting, parented dynamic
motion, native collider offset edits, nonphysical early rejection, entity-vector
relocation, cache survival across culling/failed previews, successful removal,
uniform backing after moves, custom/builtin tint, exact batch distinctions,
texture generations, invalid bindings and non-NUL-terminated lookup views.

The expanded gate initially exposed a 30-second timeout in the scatter startup
test. Its existing fixture prepared the full 512 x 512 example before truncating
placements. The direct test eventually passed. It now uses a small authored
4 x 4 project, exercising real asset preparation/project loading/prefab startup
without incidental full-landscape generation. Metadata uses actual source hashes;
flat terrain is explicitly above sea level. The test passes in about 0.05 seconds.
Production examples, their densities and the timeout were not changed.

Source validation passed for `terrain_graph_3d` (15 files) and
`minimal_2d_android --platform android` (12 files). This does not substitute for
physical Android or Windows rendering qualification.

## Remaining work

The engine still performs several world scans and per-view uniform-composition
copies. Persistent subsystem indexes need a coherent structural-mutation
contract before replacing those scans. Terrain still publishes ordinary surface
mesh/collider entities; dense vegetation and live LOD/streaming remain separate
integration work. This pass does not claim that all engine inefficiencies or the
reference landscape quality target are complete.

The native-rebuild/relaunch clarification was published to the maintained
Play-mode guide in release `20261011-runtime-efficiency`. Composer checks and
105 Twig templates passed, production cache warmup succeeded, the health endpoint
returned `status: ok`, and the live page was checked for the clarification.
