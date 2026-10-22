# Shared runtime efficiency and qualification

Current Silvermere evidence is recorded below under **Final Silvermere native
checkpoint**. The opening sections preserve the separate 2026-10-11 rewrite
qualification; their unchanged-component and unchanged-shadow-setting statements
apply only to that historical experiment.

## Historical rewrite: 2026-10-11

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

## Opaque instance submission inputs

The renderer now separates immutable surface inputs from pass programs, raster
adaptation and uniform spans. Its existing range-aware instance collector can
retain grouped matrices and material-section identity inside the explicit
camera depth/color owner. Eligible sources compile from raw visible copy slices:
LOD selection runs once per copy, while palette/texture bindings resolve once
per selected source LOD. A later pass adapts programs and uniforms once per group.

Terrain, skinning, dents, relief, fades, wireframe, custom programs, transparency
and non-depth-writing sources remain on their established paths. Source material
overrides and imported index ranges stay intact; opaque alpha masks keep the same
albedo and cutoff. Reset drops borrowed identities and readiness while retaining
allocation capacity. No plan is reusable across cameras or presentations.

The collector supports both ordinary and analytic-depth output programs. The
MRT path below compiles these groups for one opaque geometry pass; explicit
compatibility paths can still reuse them across depth/color passes. This is not
an early-Z prepass: the optical depth is a color attachment, while ordinary
hardware depth remains owned by the scene target.

`demi-opaque-instance-plan-tests` checks pass-specific program/uniform adaptation,
normal binding frequency, mask texture/cutoff, section ranges, identical grouped
matrices, logical counters, camera reset and stale-token rejection. Build/native
qualification is included in the final native checkpoint below; focused tests
do not establish pixel equivalence for every material or platform.


## Analytic opaque depth in the color pass

Built-in lit, PBR, skinned and terrain materials can write regular color and
normalized eye-space depth into two attachments during the same opaque draw.
The shared shader body applies identical material sampling and alpha cutout to
both outputs. Sky writes far depth explicitly, and the analytic attachment is
cleared to far even when the camera preserves its color attachment.

The capture owner retains one scene color/hardware-depth target. An opaque
framebuffer aliases those attachments plus analytic depth; the continuation
framebuffer aliases only scene color/hardware depth. After opaque submission,
a fullscreen resolve selects the nearest valid multisample depth, rather than
averaging across silhouettes. Opaque color is copied before water, then water
and transparent geometry continue against the original hardware depth. The
postprocess/underwater pass receives the completed scene color and resolved
depth. No second scene-color scratch target is allocated for this path.

`Renderer3D.water_capture_path` reports the selected path reason; capability
limits retain the explicit sampling pass. Reason 7,
`UnsupportedPreservedAuthoredColor`, is an integration/output-ownership limit:
an authored render target with `clear_mode: depth` or `none` retains the legacy
destination path. Its prior color is not replaced by a separately owned MRT
scene. Color-cleared authored targets remain eligible for MRT. Backbuffer
scratch behavior is unchanged. Custom fragment programs have no
analytic-depth output contract and are reported separately. Their existing
water-depth-capture error remains explicit because arbitrary custom coverage
cannot be inferred by a substitute built-in shader. Shader creation failures
are errors, not reasons to silently reduce shading or sample quality.

MRT requires at least two attachments and validated R32F render-target support.
At multisample settings it also requires target and explicit multisample sampling
support. Actual sample-count querying is currently qualified for Vulkan/SPIR-V;
other backends retain the explicit depth pass at MSAA settings rather than
assuming requested samples equal allocated samples. Validated 1x targets do not
need a multisample query. Authored MSAA is never silently disabled.

The complete `Renderer3D.water_capture_path` reason mapping is:

| Value | Reason |
| --- | --- |
| 0 | AnalyticMrt |
| 1 | InsufficientAttachments |
| 2 | UnsupportedLinearDepthTarget |
| 3 | UnsupportedMultisampleTarget |
| 4 | UnsupportedMultisampleSampling |
| 5 | UnsupportedCustomDepthOutput |
| 6 | UnsupportedMultisampleCountQuery |
| 7 | UnsupportedPreservedAuthoredColor |

Generated shader-interface qualification includes ordinary and MRT lit,
instanced, skinned, terrain, sky, and multisample-resolve families. Resource and
capture regressions exercise resize/lifetime, attachment binding, per-attachment
clear and fallback decisions. The final native capture below verifies Vulkan
MRT operation; it does not qualify every backend or arbitrary custom shader.


## Receiver-hull shadow caster culling

Each cascade retains its existing stable square projection, texel snapping,
depth range and receiver blend overlap. Additional CPU side planes bound the
convex hull of the eight true receiver-slice corners projected into light space.
These planes are perpendicular to the light, so off-camera casters aligned with
an on-camera receiver remain eligible throughout the existing caster depth
range. No receiver history, geometry reduction or temporal visibility state is
introduced.

The hull expands for the actual shader sampling footprint: continuous PCF can
reach two texel centers on each axis and receiver normal offsets can reach
1.5 world texels. Hard sampling uses half-texel quantization and half-texel normal
offsets. Plane construction and evaluation use a camera-relative origin with
explicit rounding/float input allowances. Existing source/copy spheres and
expanded BVH node AABBs remain conservative; unknown bounds remain accepted.
Prepared-view identity includes the entire hull value, including its origin,
planes and count.

Profiler gauges under `Renderer3D.view.<id>.instances` report
`hull_sources_rejected`, `hull_copies_rejected_by_index` and
`hull_copies_rejected`. Counts require no per-copy timing scopes. Focused tests
cover rotated camera bases, perspective/orthographic projections, cascade blend
overlap, light extrusion, filter support, large-world translation, straddling
bounds, authored instance order and prepared-view invalidation. Native evidence
is recorded below; continuous visual acceptance across camera/light motion and
cascade overlap remains separate from timing qualification.


## Final Silvermere native checkpoint

2026-10-22, Linux Release, Vulkan, 3840 × 2160 fullscreen, authored 4x MSAA,
one native process and no concurrent builds/benchmarks. The current prepared
landscape contains 149,125 placements, including 120,825 grass patches. Only the
showcase grass disables casting; trees, other vegetation and rocks retain it.
Camera and optical depth still include grass. Main art/placement details live in
[Silvermere qualification](silvermere-showcase.md).

`/tmp/silvermere-shadow-final.csv` records 6,500 frames. Activated frames begin at
2395; excluding the first 180 active frames leaves 3,925 samples. MRT reason is
zero throughout the active capture, duplicate opaque depth geometry is absent,
and no native validation errors were observed.

| Warm active metric | Median | p95 |
| --- | ---: | ---: |
| Frame work | 21.387 ms | 27.433 ms |
| Update | 0.716 ms | 1.274 ms |
| CPU render preparation | 18.027 ms | 25.097 ms |
| GPU | 18.567 ms | 27.883 ms |
| Directional-shadow CPU | 1.606 ms | 2.659 ms |
| Main GPU view | 15.524 ms | 22.619 ms |
| Far shadow GPU view | 0.823 ms | 3.103 ms |

Matched workload bins compare the final trace with
`/tmp/silvermere-mrt-native.csv`, filtering actual activated scenes and excluding
180 startup frames in each. Bins use main visible records (20,000-wide) and main
triangles (one-million-wide), not shadow counts or equal frame numbers. Both
captures already use MRT; gains combine grass no-cast policy, sampling-aware
static shadow LOD and receiver-hull rejection. No isolated attribution is valid.

| Main workload, old → final medians | Previous MRT | Final |
| --- | ---: | ---: |
| Visible records (131 / 249 samples) | 53,118 | 53,120 |
| Main triangles | 8,057,907 | 8,056,209 |
| Shadow triangles | 45,939,082 | 2,385,761 |
| Frame work | 36.821 ms | 19.042 ms |
| CPU render preparation | 31.422 ms | 14.702 ms |
| GPU | 37.879 ms | 19.846 ms |
| Directional-shadow CPU | 20.354 ms | 1.629 ms |
| Instance upload bytes | 10,488,896 | 3,632,544 |

A second bin (888/587 samples) holds main triangles at 4.826→4.830 million and
visible records at 105,227→105,299: frame work 30.960→22.948 ms, CPU preparation
29.944→21.807 ms, GPU 19.261→18.492 ms. These are moving-tour workload matches,
not identical camera/light/time experiments or a guarantee for every view.

Other shadows remain present: final far-cascade medians are 2,200,245 triangles,
10 batches, 3,267 low-LOD casters and 5,126 examined copies. Main visibility
examines 77,739 copies. Hull counters report median 7,355 far-cascade copies
rejected by the index plus 1,029 direct-copy rejections; main hull rejection is
zero. Earlier grass-heavy shadow work was not merely renamed into the main pass.

The remaining limit is substantial main rendering: a dense matched bin near
7.44 million main triangles costs 26.857 ms GPU versus 26.295 ms previously,
although CPU preparation falls 33.488→23.650 ms. Sustained native 4K60 remains
unqualified. Smooth LOD fading, automatic model LOD generation and stronger art
composition also remain open. This checkpoint is not a claim of reference-engine
feature parity or completed runtime optimization.


## Skipped editor view and material texture lifetime

A 35-second Minimal3D editor trace recorded approximately 543 ms in
`Renderer3D.submit_collect` on frames 1994, 2149 and 2151. Each spike immediately
followed a UI frame with no Game draw. Source addresses, entity order, component
storage revision and visible mesh count stayed unchanged; GPU and simulation
work did not explain the CPU collection interval.

`EditorGameRenderer::maintainTerrainResidency` invokes inactive terrain
maintenance when a view is clipped, hidden or skips drawing. That maintenance
incorrectly destroyed every ordinary material texture and reset its usage
serials, including imported/prepared images. The next draw synchronously
prepared those images again inside collection. Inactive terrain maintenance now
retires only terrain-owned geometry, demand and material arrays. Uploaded
model/material assets intentionally remain resident through hidden/skipped views,
under their asset publication/reset/unload owner and normal presentation role
retention. Asset replacement and renderer shutdown still release them.

The renderer regression checks three skip/resume cycles, unchanged upload count
and the same live texture binding, followed by explicit asset replacement and a
new upload. Existing hidden-terrain geometry retirement coverage is retained.
The focused renderer regression passed in the coordinated Linux Release
nine-suite gate. The repeated native trace is separate; no post-fix timing result
is claimed here yet.


## Active terrain views with skipped draws

Editor terrain ownership now follows explicit view/warmup demand, rather than
successful draw completion. Game View records a 3D request before a resize can
skip submission. Authoring view visibility retains an already active 3D demand
while its target is being prepared. Switching to 2D/HUD explicitly retires that
demand. Closing/hiding a view, replacing its owner or unloading still releases
terrain fields, material arrays and geometry through the existing budget owners.
No elapsed-time grace period or additional resource cache is introduced.

`EditorTerrainViewDemand` supplies the shared event policy for Game and authoring
renderers. Focused coverage includes visible skipped draws, pending targets,
2D/HUD changes, explicit hidden warmup, inactivity and release/reset. The combined
renderer fixture uses four GPU terrain patches plus an ordinary shared material:
active skipped/resumed frames keep texture and mesh upload counts unchanged;
actual inactivity releases the terrain field, normal warmup restores it, and the
ordinary material remains asset-owned. The new terrain-specific regression awaits
the coordinated build and native follow-up; ordinary material correction and LOD
formatting had already passed the parent's nine-suite focused run.
