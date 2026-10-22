# Repository documentation

The [website documentation](https://demiengine.de/docs) is the maintained
source for game authoring, public feature behavior, CLI use, and API guidance.
Its page bodies live in `tools/package-store/templates/docs/content/`; new pages
are registered in `tools/package-store/src/Docs.php`.

This directory keeps material that has a different job:

- [Static shadow model LOD policy](shadow-model-lod-policy.md) explains the
  cascade sampling heuristic, exclusions and pending visual qualification.

- [3D showcase and authoring plan](../3d-showcase-plan.md) inventories verified
  capabilities and phases the requested landscape, cinematic and editor work.

- [Silvermere terrain showcase](silvermere-showcase.md) records the camera tour,
  real placement counts, visual qualification and prioritized feature gaps.
- [Runtime ECS migration](runtime-ecs-migration.md) records typed world storage,
  ownership contracts, the controlled prototype and integrated measurements.
- [Shared glTF import qualification](gltf-import-consolidation.md) records the
  parser consolidation, preserved scene policies and collider fast path.
- [Library and fallback audit](library-and-fallback-audit.md) distinguishes
  concrete code-replacement opportunities from capability and failure paths.

- [Amber Pass loading and performance](amber-pass-loading-performance.md) records
  asynchronous preparation, riding regressions, visibility costs and separate
  main/shadow geometry measurements.
- [Amber Pass mounted demo](amber-pass-qualification.md) records native mountain
  terrain, reusable horse art, riding checks, foliage coverage and AC-power GPU
  measurements, plus shared asset lifetime and shader fixes.
- [Dense-terrain Game View performance](terrain-game-view-performance.md) records
  simulation membership queries, revisioned instance inputs, clipped runtime
  hierarchy rows and embedded Play profiling.
- [Dense-terrain viewport navigation](editor-viewport-navigation-qualification.md)
  records presentation-local render preparation, dedicated instance buffers,
  retained hierarchy rows and navigation performance checks.
- [Editor hierarchy startup qualification](editor-hierarchy-startup-qualification.md)
  records the dense-terrain first-window stall, hierarchy indexing, deferred
  reference pickers and measured startup/UI costs.
- [Terrain Milestone 6 qualification](terrain-milestone6-qualification.md) records
  procedural vegetation, stable placement rules, scene-object conversion,
  package prefabs, rendering costs and native editor/source/cooked checks.
- [Terrain Milestone 5 qualification](terrain-milestone5-qualification.md) records
  water authoring, scene capture/optics, underwater views, opt-in gameplay,
  cooked loading and measured native rendering costs.
- [Water-driven terrain rules](terrain-water-rule-integration.md) records
  connected water inputs, shoreline-mask ownership, early scatter rejection
  and derived-cache invalidation.
- [Water rendering architecture](terrain-water-rendering-design.md) records
  the opaque-scene capture, sampleable depth, MSAA and pass-order boundaries
  used by Milestone 5 reflection/refraction and underwater rendering.
- [Terrain Milestone 4 qualification](terrain-milestone4-qualification.md)
  records GPU material layers, real starter art, color semantics, native/cooked
  checks, measured texture residency, matched-scene performance comparisons and
  retained performance-debug switches.
- [Shared terrain and mesh material qualification](terrain-shared-material-qualification.md)
  records canonical Materials, mapped PBR/triplanar shading, semantic mipmaps,
  native/cooked comparisons and the remaining Milestone 4 boundaries.
- [Terrain Milestone 3 qualification](terrain-milestone3-qualification.md)
  records drainage-connected rivers, physical water profiles, bounded sediment
  transport, native shape checks and generation acceptance.
- [Terrain generation quality qualification](terrain-generation-quality-qualification.md)
  records production Preview/Final safety, drainage/thermal fixes, simulation
  masks and representative original-grid generation timings.
- [Terrain erosion library evaluation](terrain-erosion-library-evaluation.md)
  compares pinned upstream dependencies and integration gaps, with actual
  native/libtcod runs and explicit limits on performance and quality conclusions.
- [Terrain polygon mask qualification](terrain-polygon-mask-qualification.md)
  records shared spatial-mask validation, basin/plateau/island presets, graph
  editing and native generation checks.
- [Terrain shadow receiver qualification](terrain-shadow-receiver-qualification.md)
  records view-direction precision, stable receiver derivatives and filtered
  facet correction, with native before/after evidence and contact limitations.
- [Terrain landform qualification](terrain-landform-qualification.md) records
  shared ridge/warp controls, editable valley masks, deterministic generation
  coverage and the new probe's remaining material and shadow limitations.
- [GPU terrain qualification](terrain-gpu-rendering-qualification.md) records
  native full-detail submission and frame-time comparisons, repeated runs and
  the compatibility/material limitations of the implemented path.
- [GPU terrain rendering design](terrain-gpu-rendering-design.md) explains
  shared displaced patches, instanced material routing and capability fallbacks.
- [Terrain LOD qualification](terrain-lod-qualification.md) records error bounds,
  shared chunk boundaries, live camera selection, GPU residency and moving-camera
  measurements, with CPU streaming still pending.
- [libuv Android minimum API qualification](libuv-android-qualification.md) records
  the adoption of API 24 to match the pinned dependency and the previous
  barrier/interface-enumeration compile failures.
- [Shared runtime efficiency qualification](runtime-efficiency-qualification.md)
  records readable renderer/physics rewrites, same-build measurements, native
  editor verification and mutation/lifetime regression coverage.
- [Terrain performance qualification](terrain-performance-qualification.md)
  records the CPU bottlenecks, Debug/Release comparison and native editor check.
- [Terrain surface material qualification](terrain-surface-materials-qualification.md)
  records automatic role assignment, finite water coverage, material rebinding,
  cooked payloads, editor warnings and the remaining blending boundary.
- [Terrain palette qualification](terrain-palette-qualification.md) records
  format-2 migration, native editing, cooked loading and remaining rendering gates.

- [Terrain palette separation proposal](terrain-palette-proposal.md) defines
  the accepted surface-material versus object-placement format checkpoint.

- [Editor usability redesign](editor-usability-redesign.md) records Inspector,
  viewport, creation, execution and dynamic-lighting changes and qualification.

- [Colony logistics qualification](colony-logistics-qualification.md) records
  physical material conservation, needs, time controls and editor fixes.

- [Colony navigation qualification](colony-navigation-qualification.md) records
  utility connections, engineer jobs, reusable ground agents and API migration.

- [Colony construction qualification](colony-construction-qualification.md) records
  the playable utility-building probe, HUD click blocking and native verification.

- [UI prefab authoring qualification](ui-prefab-authoring-qualification.md) records
  target-based overrides, reparenting, Unpack, tab previews and native verification.

- [Orbit UI qualification](orbit-ui-qualification.md) records package UI reuse,
  shared docking/rendering fixes, native captures and store publication evidence.

- [Shared authoring foundations](shared-authoring-foundations.md) describes
  reusable graph, history, palette and color responsibilities and their adapters.

- [Typed asset authoring qualification](editor-asset-authoring-qualification.md)
  records native creation/editing, reference dependencies, save retry/recovery
  and the remaining terrain rendering boundary.

- [Mesh surface qualification](mesh-surface-qualification.md) records material
  inheritance, direct metallic/roughness lighting, transparent ordering and
  near-camera picking coverage.

- [NVIDIA HUD lifecycle investigation](editor-nvidia-hud-lifecycle-investigation.md)
  records the reduced Play/Stop reproducer, diagnostic comparisons and the
  qualified Vulkan program/pipeline lifetime correction.

- [Editor commands, clipboard and docking](editor-shortcut-qualification.md)
  records shortcut ownership, clipboard identity/remapping, independent panel
  identities, contextual palettes and scoped verification.

- [Terrain qualification](terrain-qualification.md) records finite-heightfield
  authoring ownership, tests, surface-query measurements and remaining scope.

- [Terrain material binding](terrain-material-binding-qualification.md) records
  ordinary-material publication, texture scale, retained geometry and prepared
  payload appearance metadata.

- [Terrain water publication](terrain-water-publication-qualification.md)
  records transparent surface ownership, native publication and prepared loading.

- [Terrain data and query boundaries](terrain-boundaries-qualification.md)
  records the lightweight hydrology test targets, shared query snapshots and
  build-iteration investigation.

- [Terrain water gameplay](terrain-water-gameplay-qualification.md) records
  world-space Lua queries, explicit immersion tracking, cache ownership and
  sensor lifecycle qualification.

- [Lua task qualification](lua-task-qualification.md) records scheduler and
  asynchronous I/O ownership, overhead measurements, and validation boundaries.

- [Architecture](architecture.md), [renderer migration](bgfx-migration.md),
  [capability gates](capability-gates.md), and the
  [text stack ADR](adr/0007-production-text-stack.md) record internal decisions
  and engineering boundaries.
- [Milestone 1 baseline](3d-milestone-1-baseline.md),
  [Milestone 2 qualification](3d-milestone-2-qualification.md), and the other
  `3d-*-scaling.md`, `3d-*-qualification.md`, and timing/optimization reports
  record scoped hardware, workloads, measurements, and limits. They are evidence,
  not general performance guarantees.
- [Compatibility policy](compatibility.md), [shipping audit](shipping.md), and
  [device matrix](device-matrix.md) retain repository-specific rules and
  qualification detail.

Other topic guides remain here while their unique content and incoming links
are checked against website pages. For a public feature change, update the
website page first. Keep an internal guide only when it records distinct design,
implementation, or qualification information. Do not remove a topic guide just
because a similarly named website page exists.
