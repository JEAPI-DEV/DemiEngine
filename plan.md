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

- [ ] Add `performance_3d_lab` with the workloads above; retain the existing 3D
  Galton board as a regression probe, not the sole performance benchmark.
- [ ] Extend existing profiling only where metrics are missing; retain reproducible
  reports and distinguish measured bottlenecks from code-review hypotheses.
- [x] Compile the pinned Blast subset without PhysX/CUDA/Omniverse and audit the
  dependency closure. Run a headless damage/split smoke test.
- [ ] Spike a small Blast asset splitting into Jolt bodies. Verify mass, pose,
  collision, lifetime cleanup, and stable chunk-to-body identity.
- [ ] Record the desktop reference machine and Android feasibility result.
- [ ] Measure Blast work-unit boundaries and worst-case call durations; establish
  separate provisional analysis, transition, physics, and response-latency budgets.

Exit gate: baseline evidence and a portable integration proof. Do not replace
Jolt or rewrite unrelated subsystems based on entity counts alone.

### Milestone 2: Engine Scaling

- [ ] Optimize the measured hot paths one at a time with before/after reports.
- [ ] Review material-compatible instancing for imported meshes, redundant
  transform/world scans, per-frame allocation, and repeated string lookups.
- [ ] Review physics synchronization and contact extraction separately from Jolt
  solver time. Avoid unused reports without changing collision behavior.
- [ ] Introduce GPU skinning and animation LOD/update budgets when validated by
  the character benchmark. Existing CPU skinning and static-only model LOD are
  explicit limitations to address, not capabilities to assume solved.
- [ ] Keep collision-critical simulation and input responsive when AI or distant
  visual animation uses a reduced update rate.

Exit gate: declared scaling workloads meet agreed budgets with collisions intact;
focused regression tests and existing 2D/3D probes remain valid.

### Milestone 3: Localized Destruction and Asset Pipeline

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

- [ ] Extend current asset streaming with spatial activation and memory/upload
  budgets. Add screen-size LOD with hysteresis, distant geometry grouping, and
  separate shadow budgets as justified by traversal measurements.
- [ ] Keep rendering LOD separate from simulation relevance. Offscreen destruction
  must not lose gameplay state or leave invisible collision behind.
- [ ] Provide editor inspection/debug views for fracture chunks, anchors, bonds,
  damage, active bodies, LOD selection, and resource/performance budgets.
- [ ] Add save/load for damaged structures and retained debris using versioned
  data, stable IDs, and tested migrations; verify reload after streaming/unload.
- [ ] Update component metadata, schemas, validators, editor, Lua bindings/stubs,
  documentation, cooking/packaging, and tests together for every public contract.
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

#### Graphics test hardware

The user currently has UEFI configured for **dedicated GPU only**. Continue
development and visible profiling on the GeForce RTX 3070 Ti Laptop GPU; do not
request a firmware change or retry Radeon selection during this configuration.
The Radeon startup timeout observed while forcing its Vulkan driver is not a
valid iGPU performance/compatibility result under that firmware setting.

- [ ] After the user switches UEFI to hybrid mode at a convenient reboot, verify
  that the Radeon 680M is usable and explicitly selected in the recorded GPU IDs.
- [ ] Repeat the same 1080p active-body and pile captures on the iGPU, with real
  deltas, VSync on/off, warmup exclusion, GPU timestamps, and lost-time checks.
- [ ] Record hybrid-mode startup, surface/resize behavior and sustained performance;
  compare CPU/GPU costs without extrapolating the dedicated-GPU results.

Non-NVIDIA/iGPU qualification remains pending until those tests actually run.

Start Milestone 1. Produce the performance baseline and CPU-only Blast/Jolt
integration result before committing to detailed optimization or fracture scale.

Milestone 1 is in progress. The initial implementation provides three primitive
lab workloads, a reproducible headless runner, profiler CSV percentiles and active
body counts, a pinned CPU-only Blast smoke test, and a singleton-chunk handoff to
the existing Jolt wrapper. Android ARM64 cross-compiles, but has not run on a
device. Compound splitting, complete performance instrumentation, animated/imported
asset and streaming workloads, and GPU qualification
remain pending. Do not mark the full integration or 2,000-entity target complete.
See [the initial evidence and next steps](docs/3d-milestone-1-baseline.md).

Repeated Release/headless primitive baselines and a tested Lua entity-service
lookup optimization are now recorded in [Release scaling](docs/3d-release-scaling.md).
The moving-mesh result improved substantially; physics controls did not improve
and dense piles remain over the provisional frame budget. Blast chain/grid
benchmarks distinguish bounded damage arrays from whole-actor split calls;
production stress/transition budgets and the scheduler itself remain pending.

The subsequent [physics optimization](docs/3d-physics-optimization.md) fixes a
per-step activation bug that prevented natural sleep, preserves resting contacts
and support-change wakeups, and reduces contact/synchronization overhead. Matched
Release runs reduced 2,000-body physics-step p95 by roughly one third; the dense
pile's complete CPU frame remains over budget and GPU qualification is still open.

A follow-up [sorted contact-phase merge](docs/3d-contact-merge.md) removes hash
construction for ordered streams while preserving the general unsorted path.
Matched dense-pile runs showed roughly 20% lower phase-assignment p95 and 5%
lower whole CPU-frame p95 (15.913 ms). This is CPU-only evidence, not completion
of the rendered 60 FPS or broader workload qualification gates.

The [visible 1080p dedicated-GPU captures](docs/3d-visible-timing.md) now separate
CPU update/render preparation, graphics advancement/backend waits, and delayed
GPU timestamps. All 12 recorded primitive-body runs met the provisional frame
interval targets after warmup, with no discarded simulation time. This qualifies
only the tested reference workload on the dedicated GPU, not richer 3D content
or other hardware. Hybrid/iGPU and physical Android checks remain pending.
