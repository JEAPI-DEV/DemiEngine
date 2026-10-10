# Repository documentation

The [website documentation](https://demiengine.de/docs) is the maintained
source for game authoring, public feature behavior, CLI use, and API guidance.
Its page bodies live in `tools/package-store/templates/docs/content/`; new pages
are registered in `tools/package-store/src/Docs.php`.

This directory keeps material that has a different job:

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
