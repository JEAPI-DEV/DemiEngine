# DemiEngine Roadmap

## Overall Completion Rule

The roadmap succeeds when developers can create, validate, run, test,
diagnose, package, and maintain the supported game scenarios using documented
project data, assets, Lua packages, and public APIs. Ordinary game requirements
must not require edits to engine internals, and every reusable workflow must be
proven on Linux and Android where the platform supports it.

## Active Direction: Professional Desktop 3D

Expand the experimental 3D foundation into a polished, responsive workflow for
3D action games and destructible environments. Preserve supported 2D workflows.
This is a planned direction, not a claim that the capabilities below already
exist or that Demi matches an established AAA engine.

### Status Audit — source snapshot `fef9c67`

Checked items below have an implementation and supporting tests or recorded
evidence. Partial items are split into delivered work and remaining scope rather
than marking an entire milestone complete. Benchmark results apply to their
recorded builds, workloads, hardware, and settings; this documentation audit did
not rerun benchmarks or qualify every feature at the current revision.

| Milestone | Current status |
| --- | --- |
| 1 — Baselines/feasibility | Complete: baseline evidence and portable Blast/Jolt integration proof, including physical Android CPU execution. Production and expanded-workload qualification belongs to later milestones. |
| 2 — Scaling | Measured lookup/contact/sleeping improvements, initial Vulkan GPU skinning and scoped desktop qualifications delivered; animation budgets, broader rig/material support and mixed game workloads remain open. |
| 3 — Localized destruction | Collider-asset prerequisites delivered; production fracture assets, scheduler, and compound-body transitions are not implemented. |
| 4 — Visual/gameplay quality | Third-person mechanics foundation and native visual denting delivered; production animation/visual qualification remains open. |
| 5 — Structural collapse | Impact-energy telemetry exists; structural connections, stress, fracture-driven collapse, and debris/character policy remain open. |
| 6 — Landscape/workflow | Asset-service and static distance-LOD foundations plus editor workflow improvements exist; landscape-scale and destruction-specific qualification remain open. |

The reference outcome is a playable demolition environment: accurate player
movement, good materials and lighting, localized hammer/rocket damage, collapsing
structures, and physical debris. Scenes stay editable in `main.scene.json`;
examples exercise reusable engine systems and packages rather than hiding engine
workarounds in Lua.

### Agreed Technology Boundaries

- Keep Jolt as the 3D rigid-body, collision, query, and constraint backend.
- Integrate NVIDIA Blast for damage, chunk/bond fracture, and actor splitting.
  Use its physics-independent low-level/toolkit layers through a Demi-owned
  adapter. Do not adopt the PhysX-specific integration extension.
- No CUDA, NVIDIA GPU, PhysX runtime, or Omniverse runtime requirement. Runtime
  destruction must work on the CPU independently of the graphics vendor.
- Pin and audit the selected Blast revision, license, dependencies, build flags,
  serialization format, and required extensions before integration. Prove the
  actual selected build works without the excluded dependencies; library naming
  alone is not evidence of portability.
- Assess Blast's physics-independent stress extension after basic support-loss
  destruction works. Do not build a second fracture graph alongside Blast.
- Jolt and Blast types stay behind runtime subsystem boundaries. Stable Demi
  asset/entity/chunk IDs connect authoring, physics, rendering, saves, and Lua.
- Qualify Linux desktop first, including non-NVIDIA graphics. Investigate Android
  build feasibility early; mobile budgets and support are separately qualified,
  never inferred from desktop success.

Blast documents its low-level/toolkit layers as physics- and graphics-independent;
their actor split results must be translated into our own physics/render objects.
Its stress extension likewise does not require an external physics library.
See the [official Blast SDK documentation](https://docs.omniverse.nvidia.com/kit/docs/blast-sdk/latest/index.html).

### Performance Contract

The initial objective is approximately 2,000 moving entities at responsive frame
rates, measured against declared workloads rather than an arbitrary scene count.
For the barrel impact tower specifically, the accepted near-term target is about
1,000 bodies (the 1,024-body fixture), with 2,000 as a stretch target. Neither
target implies the same throughput for animated characters or destructible buildings.

- Select and record a reference CPU/GPU/RAM configuration, optimized build,
  resolution, graphics settings, physics timestep, and solver/CCD settings.
- Provisional desktop target: 1080p, 60 FPS; p95 frame time <= 16.7 ms and p99
  <= 25 ms in the steady-state reference workloads. Confirm feasibility against
  the baseline before treating these as release gates.
- Test 250, 500, 1,000, and 2,000 moving mesh entities separately from the same
  counts of active colliding rigid bodies. Test animated characters on their own
  scaling curve; 2,000 simple bodies does not imply 2,000 full AI characters.
- Include repeated and varied meshes/materials, all-visible cases, scattered
  bodies, dense contact piles, streaming traversal, and destruction bursts.
- Record p50/p95/p99/max frame time, CPU/GPU split, physics solver versus engine
  synchronization/contact dispatch, Lua, animation, draw calls, active/sleeping
  bodies, contacts, allocations, resident memory, and upload/load hitches.
- Record simulation time versus wall time, fixed-step backlog, and dropped time.
  Never meet a performance target by silently slowing simulation or disabling
  required collision. Report sleeping, culling, and update-frequency policies.
- Use fixed seeds, recorded input/camera paths, warmup, repeated runs, and saved
  reports. Headless runs measure CPU behavior, not visible rendering performance.
- Set explicit destruction burst budgets from measurements before increasing
  asset complexity. Include recovery time and repeated-explosion memory growth.

### Destruction Scheduling and Load Control

Treat the destruction scheduler as a first-class engine responsibility, not a
late optimization. Separate damage/stress analysis, fracture and body-transition
work, Jolt simulation, and visual effects into independently measured budgets.
Retaining work between frames is required; keeping that state on the GPU is not
assumed. Portable GPU acceleration remains an option for measured suitable work.

Reference: Eric Arnold's GDC 2011 talk,
[Living in a Stressful World](https://gdcvault.com/play/1014658/Living-in-a-Stressful-World).
The full talk and Q&A describe roughly 5 ms steady-state / 15 ms spike budgets
for physics, separately keeping stress below 1 ms per frame (around 24:30).
Stress is processed incrementally across frames (6:10), with affected objects
queued (17:20); Arnold recalls a limit of roughly 400 atomic pieces per frame
(34:20). He mentions a physics load balancer without detailing its implementation.
These are historical Guerrilla/Armageddon references, not remaster GPU evidence,
Demi measurements, or constants to copy into the engine.

- Queue structures affected by damage, repair, support changes, or relevant
  dynamic loads. Coalesce redundant requests instead of scanning every structure
  or restarting a busy structure indefinitely.
- Preserve analysis progress across frames. Prioritize nearby gameplay-relevant
  work while aging queued requests to prevent starvation. Define maximum acceptable
  response latency as well as a per-frame work budget.
- Keep immediate hit feedback and collision-critical updates responsive. Only
  defer work with an explicit gameplay contract, such as progressive stress
  failure. Never hide physics overload by slowing fixed-step simulation.
- Version pending work against structural topology. Invalidate or safely restart
  affected analysis after another hit, split, repair, or streaming change; discard
  stale results rather than committing them to the wrong chunk/body mapping.
- Determine the actual bounded work units exposed by the chosen Blast APIs.
  Do not assume an individual SDK call can be interrupted mid-execution. Measure
  worst-case calls and use supported batching, job boundaries, asset complexity
  limits, or a different scheduling approach if one call exceeds the budget.
- Commit completed body/topology changes transactionally at safe fixed-step
  boundaries. Define the visible and collision state while a transition is pending.
- Track analysis time, physics time, transition time, queue depth, oldest request
  age, pieces/bonds processed, invalidations, and budget overruns separately.
  Load control must account for active debris/contact growth, not only stress cost;
  preserve important debris and reduce cosmetic work first.
- Require deterministic queued inputs/work ordering in focused tests; wall-clock
  scheduling alone must not be presented as cross-machine determinism. Keep
  multiplayer authority/replication outside the initial implementation scope.

Acceptance: simultaneous explosions and repeated hits on a large structure must
stay within agreed work and response-latency budgets without starving smaller
structures, applying stale results, leaking resources, or losing collision.
Include cancellation/unload, repair during analysis, and topology changes while
work is pending. Faster hardware is headroom, not a substitute for bounded work.

### Milestone 1: Baselines and Dependency Feasibility

Complete for the baseline/dependency-feasibility scope. This is not full engine
or Android game qualification; follow-on requirements are tracked below.

- [x] Add `performance_3d_lab` with moving-mesh, active-rigidbody, and pile
  workloads, primitive/imported-barrel variants, and a projectile tower. Retain
  the existing 3D Galton board as a separate regression probe.
- [x] Extend profiling and reproducible runners with CPU/GPU/frame percentiles,
  render preparation/backend waits, physics simulation versus integration costs,
  active/contact counts, lost simulation time, and physics-capacity errors.
- [x] Compile the pinned Blast subset without PhysX/CUDA/Omniverse and audit the
  dependency closure. Run a headless damage/split smoke test.
- [x] Spike a small Blast asset splitting into singleton Jolt box bodies. Verify
  assigned mass/impulse response, settling pose, floor collision, cleanup over
  repeated world lifetimes, and stable chunk IDs. This is not compound splitting
  or intact-body replacement.
- [x] Record the Ryzen 7 6800H / Radeon 680M / RTX 3070 Ti Laptop reference
  machine and successful standalone Blast Android ARM64/API 23 cross-compilation.
- [x] Extend the tool-level probe to intact Jolt compounds splitting into connected
  groups; verify mass/inertia, inherited motion, stable chunk queries, collision,
  cancellation, body-capacity rollback and native body cleanup.
- [x] Run that native Blast/Jolt compound probe and CPU measurements on a physical
  Pixel 7 (Android 15, ARM64). This does not qualify Demi runtime integration,
  rendered gameplay, sustained thermals or memory behavior on Android.
- [x] Measure Blast damage-array batching and synchronous whole-actor split
  calls on synthetic chains/grids. Retain sampled maxima and the explicit
  exclusions from those timings.

Evidence: [initial feasibility](docs/3d-milestone-1-baseline.md),
[Blast dependency/probe audit](tools/3d-feasibility/README.md),
[Release scaling/work boundaries](docs/3d-release-scaling.md),
[compound transitions / physical Android CPU](docs/3d-compound-transition-feasibility.md), and the later
desktop reports below. The baseline/feasibility exit gate is complete.

Exit gate: baseline evidence and a portable integration proof. Do not replace
Jolt or rewrite unrelated subsystems based on entity counts alone.

### Milestone 2: Engine Scaling

- [ ] Complete animated-character and richer mixed-content performance coverage,
  including sustained runs. Streaming and destruction workloads are tracked in
  Milestones 6 and 3 respectively.
- [x] Add matched independent-animation/frozen crowd probes, per-frame CPU
  skinning/rebuild/upload scopes, full-population/playback capture checks, and
  reuse immutable model attributes instead of rebuilding them every pose.
  [Initial animation scaling](docs/3d-animation-scaling.md) records scoped
  16/64-character CPU results; the GPU follow-up is recorded below.
- [x] Batch independent character pose/normal/vertex preparation on the existing
  CPU worker pool, retaining render-thread GPU ownership, full-rate animation,
  bounded temporary output and failure/reload regression checks. Matched short
  [desktop captures](docs/3d-parallel-character-preparation.md) reduce 64-character
  frame p95 to about 16 ms on the CPU path; the GPU follow-up is below.

- [x] Optimize measured hot paths with before/after reports: Lua entity lookup,
  accidental body reactivation, body/contact bookkeeping, and sorted contact
  phase merging. Further optimization remains workload-driven.
- [x] Review imported-model instancing and measured lookup/allocation paths.
  The reference barrel captures render the full population in two batches;
  arbitrary explicit-material overrides and animated meshes are not thereby
  qualified or guaranteed to share an instanced submission.
- [x] Profile physics synchronization/contact extraction separately from the
  simulation scope, remove callback string work and redundant tracking, and
  retain reporting opt-outs without disabling collision. The simulation scope
  includes Jolt callbacks; it is not pure solver timing.
- [x] Record short-run 1080p qualification on both GPUs for the covered
  2,000-body/barrel workloads and the 1,024-barrel impact tower. Reject larger
  cases that discard fixed-step time rather than calling them real-time passes.
- [x] Introduce Vulkan GPU skinning for supported single-skin models (up to 128
  joints), with a shared CPU pose/reference path, authored normals, cache/reload
  handling and explicit CPU fallback. [GPU skinning evidence](docs/3d-gpu-skinning.md)
  includes NVIDIA and Radeon crowd captures. This is not universal rig/material
  support or mixed-gameplay qualification.
- [ ] Add animation LOD/update budgets and expand GPU skinning beyond the initial
  supported rig/material subset. Static-only model LOD remains a limitation.
- [ ] Keep collision-critical simulation and input responsive when AI or distant
  visual animation uses a reduced update rate.

Exit gate: declared scaling workloads meet agreed budgets with collisions intact;
focused regression tests and existing 2D/3D probes remain valid.
The recorded reference cases pass within their scope; this is not completion of
the animated/mixed-content, sustained, or mobile parts of the full contract.

### Milestone 3: Localized Destruction and Asset Pipeline

- [ ] Establish production analysis, body-transition, physics and response-latency
  budgets using destruction-burst/topology workloads; sampled SDK timings are
  not worst-case guarantees or a resumable scheduler.
- [ ] Qualify repeated-destruction memory/recovery and the production Demi
  destruction path on Android, including lifecycle, sustained performance and
  rendered gameplay, once integrated.

- [x] Provide reusable convex `*.collider.json` assets with shared validation,
  import/cook, residency/reload/lifetime handling, and a collider-filtered editor
  picker. Basic model-to-collider CLI generation for static/trigger bodies and
  collider recommendations also exist.
  These are prerequisites, not a fracture-asset pipeline; dynamic recommendations
  currently use a bounds-derived hull, not general mesh-fitted convex decomposition.
- [ ] Build `destruction_3d_lab`: a concrete wall, hammer, rocket, and steel door.
- [ ] Author pre-fractured chunks, interior surfaces, collision hulls, materials,
  bonds, and anchors through Blender/import tooling. Start with offline fracture;
  runtime damage resolution is limited by the authored chunk hierarchy.
- [ ] Import/cook through the shared asset pipeline with stable chunk IDs,
  deterministic settings/hashes, explicit dependencies, and validated manifests.
- [ ] Apply localized damage through Blast; preserve unaffected sections.
- [ ] Implement the shared scheduler's affected-structure queue, coalescing,
  resumable work, fairness, topology-version checks, and separate budget metrics.
- [ ] Translate splits into Jolt compound bodies for connected assemblies, not
  one awake rigid body per authored chunk from startup. Extend Demi's current
  collider contract for real compound bodies rather than treating child entities
  as an already-supported rigid compound assembly.
- [ ] Apply body changes at a safe fixed-step boundary; update mass, inertia,
  center of mass, inherited linear/angular motion, collision bounds, and render
  ownership. Rebuild invalidated subshape mappings after topology changes.
- [ ] Bound fracture work and body creation; test cancellation, failure, scene
  unload, repeated spawning/destruction, and resource cleanup.

Exit gate: hits open localized holes, remaining walls still collide, detached
pieces behave physically, and repeated damage stays within measured budgets.
Simultaneous-hit tests must also meet the scheduling acceptance criteria above.

### Milestone 4: Visual and Gameplay Quality

- [x] Provide the `demi.gameplay.third_person` mechanics foundation and editable
  `third_person_foundation` room: camera-relative motion, orbit/obstruction,
  stamina, directional rolls/i-frames, and windup/active/recovery melee rules.
  Package tests exist; the room uses procedural pose cues, not qualified
  production animation or bone-attached hit volumes.
- [x] Add opt-in native visual mesh denting (`Dentable3D`, `MeshDeformation`):
  impact-energy/material response, bounded runtime subdivision of existing
  models, per-instance damage/reset, and cached private GPU geometry. The
  performance lab's denting scene and tests exercise real projectile mass/speed.
  Collision remains unchanged; this is not fracture, soft-body simulation, or
  persistent/replicated structural damage. See [mesh denting](docs/mesh-denting.md).
- [ ] Create a compact visual reference environment using good source assets.
  Audit material import, metallic/roughness PBR, normal maps, color spaces,
  mipmaps, ambient/environment lighting, shadows, exposure, and anti-aliasing.
- [ ] Improve the measured weakest links; pair every visual upgrade with quality
  settings and CPU/GPU measurements rather than raising polygon counts alone.
- [ ] Qualify mouse orbit/camera collision, directional movement, rolls, animation
  blending/events, and readable attack windows in the third-person package.
- [ ] Integrate impact effects, sound, and interior fracture materials without
  masking incorrect geometry or collision.

Exit gate: the playable scene looks convincing in motion and remains within the
frame budget. Retain visual captures and interaction regression tests.

### Milestone 5: Structural Collapse

- [x] Expose pre-solver normal impact-energy estimates in native 3D collision
  events for gameplay/material response. This supplies neutral impact data,
  not a completed falling-debris/character-damage system.
- [ ] Add foundations, support-loss behavior, and breakable connections between
  structural members and attached props.
- [ ] Author density, fracture resistance, and bond strength separately. Distinguish
  material survival from attachment failure: a steel door may stay intact while
  its frame breaks and releases it.
- [ ] Add explosion falloff/obstruction and bounded impact-driven secondary damage.
- [ ] Begin with connectivity-based support loss, then evaluate Blast's stress
  extension for load-dependent failure. Connectivity alone is not a stress model.
- [ ] Integrate stress into the existing bounded scheduler, including requests
  caused by changing dynamic loads. Expose pending/near-failure state for creaks
  and dust without allowing effects to dictate structural correctness.
- [ ] Handle falling-body interaction with the character; expose neutral impact
  data while damage/death rules remain gameplay policy.
- [ ] Preserve important debris collision and persistence. Pool/fade only cosmetic
  chips and dust; do not timer-delete a supporting slab or dangerous falling door.

Exit gate: rockets damage tower supports, unsupported sections collapse, different
materials respond appropriately, and an intact door can land on/hit the player.
Test chained impacts, dense rubble, repeated collapses, and bounded recovery cost.

### Milestone 6: Landscape Scale and Production Workflow

- [ ] Complete workload-level allocation/residency and loading/upload-hitch
  qualification with representative streaming/traversal; existing asset-service
  accounting is a foundation, not the complete measurement gate.

- [x] Provide the existing asset-service foundation: explicit residency/groups,
  readiness, ownership, and upload budgets; provide static distance-based model
  LOD/culling with normal asset references. Screen-size LOD, hysteresis, spatial
  activation, and representative traversal qualification are still below.
- [x] Integrate visual scene-prefab and HUD authoring with the shared hierarchy,
  Inspector, validated commands, Undo/Redo, and source-preserving saves. Add
  per-user UI scaling plus project-folder creation and empty-folder browsing.
- [x] Make networking enabled by default with an explicit build-time opt-out;
  allow embedded Play in intentionally offline builds. Verify the Release
  networking and embedded-Play paths. These do not complete multiplayer destruction.
- [ ] Extend current asset streaming with spatial activation and workload-tested
  memory/upload limits. Add screen-size LOD with hysteresis, distant geometry
  grouping, and separate shadow budgets as justified by traversal measurements.
- [ ] Keep rendering LOD separate from simulation relevance. Offscreen destruction
  must not lose gameplay state or leave invisible collision behind.
- [ ] Provide editor inspection/debug views for fracture chunks, anchors, bonds,
  damage, active bodies, LOD selection, and resource/performance budgets.
- [ ] Add save/load for damaged structures and retained debris using versioned
  data, stable IDs, and tested migrations; verify reload after streaming/unload.
- [ ] Carry upcoming destruction/landscape contracts through component metadata,
  schemas, validators, editor, Lua bindings/stubs, documentation, cooking/packaging,
  and tests together. Existing collider/denting/editor contracts already have
  their corresponding implementation and tests; this item concerns the new work.
- [ ] Package reusable hammer/rocket/damage/effect behavior without embedding game
  rules in the engine. New destructible props should not need custom engine code.
- [ ] Qualify selected graphics vendors and supported platforms with explicit
  quality profiles. Keep unverified mobile capabilities marked experimental.

Exit gate: a polished, editable tower-demolition demo survives repeatable gameplay,
streaming, saving/loading, and stress runs within declared performance budgets.

### Deferred Until the Reference Demo Passes

- Arbitrary runtime mesh cutting and terrain excavation.
- City-scale destruction and advanced global illumination.
- Multiplayer destruction replication and cross-machine replay guarantees beyond
  explicitly tested behavior; preserve stable identities now without promising
  bitwise deterministic Blast/Jolt results across architectures.
- General AAA-engine feature parity or a physics-backend replacement.

### Immediate Next Work

Do not restart the already delivered primitive/barrel baselines. The native
compound-transition probe now covers mass/inertia, inherited motion, collision,
chunk identity, cancellation and body-capacity rollback on Linux and a Pixel 7.
The active Milestone 2 step is broader character qualification: Vulkan GPU
skinning now has supported-rig NVIDIA/Radeon evidence, including a one-minute
2,000-character visual probe. Next address animation LOD/update budgets, more
rig/material types, sustained/mobile checks and representative mixed gameplay.
Keep animation events and collision-critical work independent of visual budgets.
Milestone 3 then brings compounds into Demi's shared physics/asset/entity ownership
model, with an authored collider contract and persistent topology ownership.
Use realistic workloads to set preparation/commit/response budgets before the
affected-structure scheduler and minimal `destruction_3d_lab` wall/hammer/rocket
scene. Landscape traversal, sustained desktop
runs, and full Android game qualification remain explicit roadmap gaps. The
tool-level probe does not complete production compound/destruction support.

Use an optimized build for performance qualification and run measurements
sequentially, without simultaneous builds or other test windows. Debug remains
useful for correctness/debugging but is not a performance reference.

#### Graphics test hardware

The user enabled **hybrid graphics** on 2026-09-13. Default Demi Vulkan launches
still select the GeForce RTX 3070 Ti Laptop GPU (`10de:24a0`); bgfx prefers a
discrete GPU even when the Radeon drives the desktop. Explicit Radeon ICD
selection now renders successfully on the Radeon 680M (`1002:1681`). The earlier
Radeon startup timeout in dedicated-only mode was not a valid iGPU qualification.

- [x] After the user switches UEFI to hybrid mode at a convenient reboot, verify
  that the Radeon 680M is usable and explicitly selected in the recorded GPU IDs.
- [x] Repeat the same 1080p active-body and pile captures on the iGPU, with real
  deltas, VSync on/off, warmup exclusion, GPU timestamps, and lost-time checks.
- [ ] Record hybrid-mode startup, surface/resize behavior and sustained performance;
  compare CPU/GPU costs without extrapolating the dedicated-GPU results.

The [controlled hybrid rerun](docs/3d-hybrid-graphics.md) completed 24 valid
1080p/165 Hz captures across Radeon and NVIDIA, with no post-warmup fixed-time
discard. Radeon worst per-run p95 was 17.037 ms (one pile run misses 16.7 ms);
NVIDIA worst p95 was 15.757 ms. Radeon rendering and short-run measurements are
verified. The subsequent [physics-overhead optimization](docs/3d-physics-overhead.md)
reduced headless pile CPU-frame p95 by 15.6% and physics-step p95 by 20.5%.
Its 24 visible reruns meet p95 ≤16.7 ms / p99 ≤25 ms on both GPUs with no
post-warmup simulation-time discard: worst p95 is 14.362 ms on Radeon and
15.411 ms on NVIDIA. The tighter all-runs 14 ms goal, longer thermal soak,
systematic surface-lifecycle tests, and representative game-content qualification
remain open; occasional hitches persist.

The [5,000-body stress sweep](docs/3d-5000-body-scaling.md) now covers 2,000,
3,000, and 5,000 bodies on both GPUs with Blender open. It exposed a native
contact-capacity overflow; capacity is now 32,768 and the profiler/visible runner
detect and reject physics update errors. All 36 corrected captures have zero
such errors. Moving 3,000-body cases keep up, but 3,000-body piles lose roughly
6–7% of simulation time and 5,000-body piles lose roughly 50–56%. These do not
qualify as real-time throughput despite successful rendering. Revisiting those
stretch workloads should reduce per-step contact/synchronization and simulation
costs; do not hide the deficit by changing the fixed timestep or catch-up policy. Larger-allocation
Android memory impact, asset-heavy content, and sustained tests remain pending.

The [imported-barrel probe](docs/3d-barrel-scaling.md) adds an editable Blender
model and convex physics prefab to the same lab. All 24 1080p captures keep the
full population visible in two batches with zero physics capacity errors.
2,000-barrel moving/pile tests keep up with real time; 5,000 full-detail barrels
average about 21 FPS moving and 12–13 FPS piled, with substantial simulation
time discarded. This is not a 5,000-body real-time qualification. Keep model LOD,
convex contact costs, and per-step integration overhead as separate future probes.

Reusable convex `.collider.json` sources now import as `Collider3D` assets, with
shared parsing, source/schema validation, inspection, cooking, runtime residency,
same-ID shape reload, and live-user-safe unloading. `ModelCollider3D` attaches the
asset in scenes/scripts; the barrel lab no longer needs prefab instantiation or
duplicated point arrays. See [collider assets](docs/collider-assets.md).

The lab's **T** tower scene originally used 256 barrels and now defaults to
**1,024 (8 × 8 × 16)**, with a real CCD projectile
aimed at its base (**Space**, **R** reset, **B** back). All barrels remain dynamic;
collapse comes from contact response and gravity. The initial stack proved
unstable at default solver quality, so per-body velocity/position iteration
overrides were added with unchanged zero/default settings elsewhere. The tower
uses 64/16, passed a corrected 30-second simulated pre-shot support check, and
passes desktop E2E checks for pre-shot support, collapse, reset, and scene reuse.
The initial support check did not qualify larger towers or Android performance;
the later 1,024-barrel desktop evidence follows.

The current acceptance target is now **roughly 1,000 barrels with stable real-time
1080p performance**, with around 2,000 a stretch requirement. The
[1,024-barrel tower qualification](docs/3d-1024-tower-qualification.md) passes
three Release runs on each GPU, including a separate active-collapse window,
with unchanged collider/model and 64/16 solver quality. Worst burst p95 is
10.936 ms on NVIDIA and 11.783 ms on Radeon; no simulation time was discarded
after warmup, no capacity errors occurred, every barrel remained visible, and
all 64 top barrels fell. The interactive tower now defaults to 8 × 8 × 16.
This is a short desktop percentile-budget pass, not a long thermal soak,
Android qualification, or a 2,000-barrel impact-tower claim. Basic CLI collider
generation already exists; native cylinder assets and general mesh-fitted/editor
collider generation remain open.

A subsequent same-workload, single-run Debug/Release comparison at **1280×720
on NVIDIA**, rather than a new 1080p qualification, recorded collapse-frame p95
of 297.494 ms in Debug versus 10.495 ms in Release. Release discarded no fixed
time; Debug did. This diagnosed the sluggish unoptimized test build, not an
algorithmic optimization. Local evidence is retained in
`build/tower-dentable-debug-check/` and `build/tower-dentable-release-check/`.

#### Historical measurement trail

The reports below document individual implementation stages. Their older
"pending" lists are historical: imported-barrel probes and scoped NVIDIA/Radeon
qualification subsequently landed, as recorded above. Remaining gaps are listed
in the milestone checkboxes, not inferred from an earlier report's conclusion.

The [initial Milestone 1 slice](docs/3d-milestone-1-baseline.md) established three
primitive workloads, a reproducible headless runner, CPU profiler reports, the
pinned Blast smoke test, and singleton-chunk Jolt handoff. Android ARM64
cross-compilation succeeded at that stage. The subsequent
[compound-transition probe](docs/3d-compound-transition-feasibility.md) now adds
physical Pixel 7 CPU execution and native compound splitting. Production engine
compound integration, complete workload/memory qualification, animated-character
scaling, and streaming traversal remain open. The broader 2,000-entity game
contract is not completed by passing the simple-body/barrel cases.

Repeated Release/headless primitive baselines and a tested Lua entity-service
lookup optimization are now recorded in [Release scaling](docs/3d-release-scaling.md).
The moving-mesh result improved substantially; physics controls did not improve
in that initial comparison, and dense piles then exceeded the provisional frame
budget. Blast chain/grid
benchmarks distinguish bounded damage arrays from whole-actor split calls;
production stress/transition budgets and the scheduler itself remain pending.

The subsequent [physics optimization](docs/3d-physics-optimization.md) fixes a
per-step activation bug that prevented natural sleep, preserves resting contacts
and support-change wakeups, and reduces contact/synchronization overhead. Matched
Release runs reduced 2,000-body physics-step p95 by roughly one third; the dense
pile's CPU frame was still over budget at that stage, before the later contact
and visible-rendering work above.

A follow-up [sorted contact-phase merge](docs/3d-contact-merge.md) removes hash
construction for ordered streams while preserving the general unsorted path.
Matched dense-pile runs showed roughly 20% lower phase-assignment p95 and 5%
lower whole CPU-frame p95 (15.913 ms). This is CPU-only evidence, not completion
of the rendered 60 FPS or broader workload qualification gates.

The [visible 1080p dedicated-GPU captures](docs/3d-visible-timing.md) now separate
CPU update/render preparation, graphics advancement/backend waits, and delayed
GPU timestamps. All 12 recorded primitive-body runs met the provisional frame
interval targets after warmup, with no discarded simulation time. This qualifies
only the tested reference workload on the dedicated GPU, not richer 3D content.
Hybrid/iGPU follow-up is recorded above; full rendered Android qualification
remains pending despite the subsequent native CPU probe passing on a Pixel 7.
