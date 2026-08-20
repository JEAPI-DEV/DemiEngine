# Editor

The experimental editor is a native desktop workspace over DemiEngine's
existing project, scene, component, source, and validation contracts.

```sh
cmake --build --preset linux-debug
./build/linux-debug/demi editor --project examples/minimal_voxel
```

When run inside a project directory, `--project` may be omitted. The editor
discovers the nearest parent `demi.project.json` through the same shared
filesystem service used by `demi dev`.

## Current slice

- The hierarchy displays entities from the loaded main scene and follows
  authored 2D/3D transform parents.
- Selection drives a reflected inspector over the authored scene document.
  Boolean, integer, number, string, enum, vector, and color fields are editable
  using shared component descriptors, including their numeric bounds.
- Entity names, explicit enabled state, and explicit layers are editable.
- Components can be added from the descriptor catalog and removed from the
  inspector; both are staged, validated against the shared scene validator, and
  undoable.
- The hierarchy supports create, duplicate (with child subtree and stable-id
  remap), drag-and-drop reparenting to another entity or the scene root, and
  atomic subtree deletion through real, reversible commands.
- Inspector changes are reversible commands. Continuous drags collapse into a
  single undo step, and optional fields can be explicitly authored or reset
  without losing their original presence through Undo. Save writes
  deterministic JSON through same-directory atomic replacement.
- When the scene changes externally, a modal offers Reload from disk, Keep
  editing, Save Copy, and Cancel. The editor never overwrites the external
  version, and a failed preview rebuild restores the document and both history
  stacks.
- The Assets panel lists authored project files while excluding generated,
  build, package-cache, and Git internals.
- The Console displays diagnostics from the same `validatePath` service used by
  the CLI. Rejected authored edits also appear beside their Inspector target
  and in the Console through one document issue.
- Play saves pending valid changes and starts the normal `demi-runtime` beside
  the editor. Pause/Resume and Stop control that owned process.
- The central scene view renders authored 3D entities through the engine's
  existing bgfx renderer on the editor graphics device. It does not maintain a
  second editor-only rendering implementation.

Lua-created/runtime-generated geometry appears in the Play window rather than
the authored scene preview. A 2D preview, picking/gizmos, embedded frame
stepping, and builds remain disabled until their real engine services exist.

## Implementation roadmap

This is the practical todo list for Step 8. The detailed architectural contract
and edge-case matrix remain in [plan.md](../plan.md#step-8--functional-editor-on-runtime-contracts).
Complete milestones in order unless a regression requires otherwise. Do not
enable a control until its service, failure reporting, and tests are connected.

Status convention:

- `[x]` implemented and covered;
- `[ ]` remaining work;
- **Gate** is the evidence required before the milestone can be called done.

### Milestone 0 — Preserve the working authoring baseline

**Goal:** keep today's usable scene editing intact while later systems are
added. Owners are `EditorSceneDocument`, `EditorSceneCommand`,
`EditorSceneJson`, `EditorWorkspace`, `EditorHierarchyPanel`, and
`EditorInspectorPanel`.

**Status:** complete. The automated interaction workflow and CLI gate pass.

- [x] Open the authored main scene through the shared project/scene loaders.
- [x] Edit reflected scalar, vector, color, enum, and entity-level fields.
- [x] Create, duplicate, reparent, and delete entity subtrees by stable ID.
- [x] Add and remove components from the shared descriptor catalog.
- [x] Stage structural commands against the shared in-memory scene validator.
- [x] Rebuild the preview world from authored JSON through `loadSceneDocument`.
- [x] Provide coalesced undo/redo and conflict-safe atomic saves.
- [x] Add a scripted/manual interaction check for hierarchy drag/drop, context
  menus, component add/remove, Save, Undo, and Redo.

**Gate:** the editor command, document, workspace, scene-loader, and validation
tests pass; a save produced by the editor validates and runs through the CLI.

Run the automated interaction path with
`ctest --preset linux-debug -R demi-editor-authoring-workflow-tests`. For a
manual UI pass, duplicate a parent from its hierarchy context menu, drag one of
the duplicated children to another parent, add and remove a component in the
Inspector, exercise Undo and Redo after each action, then Save. The hierarchy,
Inspector, and Scene view must agree after every operation and after reopening
the project. The automated executable can create and retain its isolated saved
fixture for CLI verification:

```sh
./build/linux-debug/demi-editor-authoring-workflow-tests /tmp/demi-editor-smoke
./build/linux-debug/demi validate /tmp/demi-editor-smoke/demi.project.json
DEMI_HEADLESS=1 ./build/linux-debug/demi run \
  --project /tmp/demi-editor-smoke/demi.project.json --max-frames 3
```

### Milestone 1 — Finish document safety and conflict handling

**Goal:** make source editing safe enough that viewport and specialized editors
can rely on it. Keep policy in `EditorSceneDocument`; keep filesystem mechanics
in `EditorDocumentStore`; presentation belongs in a small conflict dialog/panel.

**Status:** complete. Optional-field shape, conflict choices, atomic preview
rollback, inline diagnostics, and the failure matrix are covered.

- [x] Represent insertion and removal of optional fields as reversible commands
  so undo preserves whether a value was explicitly authored.
- [x] Add a UI-free external-change decision model with explicit `Reload from
  disk`, `Keep editing`, `Save Copy`, and `Cancel` outcomes.
- [x] Never overwrite the external version. `Keep editing` retains the authored
  editor document in memory; `Save Copy` writes only to a user-selected path.
- [x] Make preview rebuild failure recover by reverting the just-committed
  command or rebuilding from the still-authoritative document; never leave
  document and preview silently out of sync.
- [x] Surface document validation beside the affected field/entity while also
  retaining the diagnostic in the Console.
- [x] Cover missing/deleted files, stale temporary files, permission failure,
  save conflicts, save followed by undo, and reload history reset.

**Gate:** every supported mutation is atomic; rejected edits change neither
canonical JSON nor undo/redo; every conflict path preserves both user and disk
content.

### Milestone 2 — Add an independent editor camera and viewport input

**Goal:** make the 3D Scene view navigable without modifying an authored camera.
Introduce one editor-only scene-view state owner; do not store its camera in the
scene or add another renderer.

- [x] Add an `EditorSceneViewState`-style value owner for editor camera pose,
  projection mode, focus, local/world mode, and snap settings. Choose the final
  name when implementing; do not duplicate this state in `EditorShell` and
  `EditorUiHostBgfx`.
- [x] Route a narrow viewport input snapshot from the UI layer: hovered/focused,
  mouse position/delta/wheel, buttons, and movement/modifier keys.
- [x] Add focused orbit, pan, zoom, and fly controls with frame-rate-independent
  movement and predictable focus capture/release.
- [x] Use the first enabled authored camera only to initialize the editor camera
  or through an explicit “Align view to camera” action.
- [x] Add frame-selected and reset-view actions.
- [x] Preserve camera state through ordinary scene edits and viewport resize,
  but reset it deterministically when opening another project.
- [x] Ensure minimized/zero-size viewports submit no invalid bgfx rectangle.

**Gate: passed.** Navigation is covered as a UI-free state transition and never
edits authored JSON. Capture begins only in the focused viewport and releases
with its navigation buttons. Camera state survives preview rebuild/refresh,
project open resets it, zero/minimized areas skip renderer submission, and the
existing bgfx device, 3D renderer, and app-host lifetime suites pass.

Controls: `Alt+Left` orbits, middle mouse pans, the wheel zooms, right mouse
plus `WASDQE` flies (`Shift` accelerates), and `F` frames the selected entity.
The viewport toolbar exposes projection, frame-selected, align-to-camera, and
reset actions.

### Milestone 3 — Implement stable-ID picking and transform gizmos

**Depends on:** Milestones 1 and 2.

**Goal:** select and position authored entities directly in the Scene view while
keeping all mutations in the existing command path.

- [ ] Add picking through an ID buffer or deterministic CPU query that returns a
  stable entity ID. Never store editor IDs in authored components.
- [ ] Keep hierarchy and viewport selection synchronized through
  `EditorWorkspace`, including empty space, deleted entities, and reloads.
- [ ] Render translate, rotate, and scale gizmos for compatible Transform2D or
  Transform3D components.
- [ ] Support local/world modes and the toolbar's position/angle/scale snapping.
- [ ] Convert world-space gizmo results to authored local transforms through the
  existing parent hierarchy helpers.
- [ ] Coalesce one pointer drag into one undo transaction and cancel it cleanly
  on Escape, focus loss, validation failure, or target deletion.
- [ ] Add runtime-backed bounds, collider, light, and camera overlays; debug
  visuals must match the runtime shapes.

**Gate:** picking remains correct after duplicate/delete/reload; gizmo
`apply -> undo` restores canonical JSON exactly; tests cover rotated parents,
non-uniform scale, snapping, cancellation, and invalid targets.

### Milestone 4 — Complete inspector and hierarchy usability

**Depends on:** Milestone 1; reference picking may reuse Milestone 3 selection.

**Goal:** make ordinary component authoring discoverable without adding
component-specific branches to the generic inspector.

- [ ] Extend shared field metadata with editor label/help, reference kind,
  canonical default, read-only policy, restart requirement, and useful numeric
  step where those values are missing.
- [ ] Add asset/entity/prefab reference pickers backed by the shared resolver.
- [ ] Filter and group Add Component choices by metadata category/domain; show
  why an incompatible component cannot be added.
- [ ] Add inline validation messages and tooltips without hiding the full
  diagnostic from the Console.
- [ ] Add explicit multi-selection with mixed-value display and one atomic
  multi-target command for fields common to every selection.
- [ ] Add keyboard hierarchy actions for rename, duplicate, delete, focus, and
  create-child while respecting text input focus.
- [ ] Keep raw arrays/objects read-only until a dedicated reversible collection
  editor exists.

**Gate:** the generic inspector contains no component-name switch; reference
choices use stable IDs; multi-edit either commits completely or changes nothing.

### Milestone 5 — Add the authored 2D Scene view

**Depends on:** viewport input and stable-ID selection from Milestones 2 and 3.

**Goal:** provide equivalent scene authoring for 2D projects through the normal
2D renderer.

- [ ] Detect whether the active scene is 2D, 3D, or mixed without storing an
  editor-only project mode.
- [ ] Render authored 2D content through `BgfxRenderer2D`, the normal asset
  registry, camera extraction, layer ordering, and parent transforms.
- [ ] Add a 2D editor camera with pan/zoom, pixel-aware grid, and frame-selected.
- [ ] Reuse stable-ID picking, selection, snapping, and command transactions.
- [ ] Add sprite/tilemap/collider/camera bounds overlays from runtime extraction.
- [ ] Define an explicit view switch for mixed scenes; never approximate sprites
  with ImGui primitives.

**Gate:** representative 2D examples render in the editor, selection and gizmos
round-trip through authored JSON, and runtime/editor ordering and collider
visuals agree.

### Milestone 6 — Add an embedded Game view and deterministic Play mode

**Depends on:** stable document safety and viewport ownership.

**Goal:** run the game inside a separate runtime world without contaminating the
authored preview. Keep the current external-process Play path working until the
embedded path has equivalent lifecycle coverage.

- [ ] Introduce a narrow runtime-host boundary only when both external and
  embedded implementations exist.
- [ ] Create a separate runtime world/render target with normal Lua lifecycle,
  fixed updates, physics, scene transitions, assets, audio, and networking.
- [ ] Implement the explicit `Stopped -> Starting -> Running <-> Paused ->
  Stopped` state machine and visible failure state.
- [ ] Route gameplay input only while the Game view is focused.
- [ ] Implement deterministic Step as exactly one fixed tick while paused.
- [ ] Expose runtime entities/components read-only in the inspector.
- [ ] Stop with complete script, scene, GPU/audio, input, and network teardown.
- [ ] Never copy runtime mutations into authored source automatically.

**Gate:** repeated Play/Stop returns all owner/resource counts to baseline; Step
advances one fixed tick; script failure and scene transition cannot corrupt the
authored document.

### Milestone 7 — Make assets and project workflows operational

**Depends on:** Milestone 1 for document safety. Cooking/building can progress
independently of viewport gizmos.

- [ ] Extract `EditorAssetsPanel` when adding state beyond the current filter and
  selection; keep source discovery in `EditorWorkspace` or a named source index.
- [ ] Add folder navigation, type filters, asset-manifest details, dependency and
  stale-cook status, and locate-in-filesystem.
- [ ] Add import/create actions through existing importer and template services;
  never write an editor-only asset database.
- [ ] Edit project preload `assets`, asset groups, and scene membership through
  versioned authored documents and reversible commands.
- [ ] Add project creation through existing project templates.
- [ ] Connect Validate, Cook, Linux Package, and supported Android packaging to
  the existing service layer with structured progress, cancellation, and
  diagnostics. Do not parse human CLI output when a service API exists.
- [ ] Enable Build Project only after selected targets and configuration produce
  real artifacts and failures remain visible.

**Gate:** editor-triggered validate/cook/package produces the same diagnostics,
manifest, and artifacts as the corresponding CLI service; cancellation leaves
no half-owned process or false success state.

### Milestone 8 — Add prefab, HUD, and specialized document editors

**Depends on:** Milestone 1 and the relevant inspector/viewport foundations.
Implement one real document type at a time; do not introduce speculative base
classes before a second format needs shared behavior.

- [ ] Prefab editor: source/expanded view, nested stable IDs, override diff,
  apply/revert, missing references, and atomic multi-file failure handling.
- [ ] HUD editor: hierarchy, anchors/layout, safe-area, DPI, locale, and sample
  data through the runtime layout engine without saving generated nodes.
- [ ] Material editor: reflected properties and runtime-backed preview.
- [ ] Animation editor: clip/state-machine editing and preview using existing
  animation assets and runtime playback rules.
- [ ] Data/dialogue, input-action, and audio editors as adapters over their
  versioned authored documents and shared validators.

**Gate:** each specialized editor saves canonical source equivalent to direct
text authoring and has no second unsynchronized data model.

### Milestone 9 — Diagnostics, profiler, recovery, and release gate

**Depends on:** the services being displayed; it can be delivered incrementally
as those services gain structured data.

- [ ] Replace placeholder profiler content with real frame/CPU/GPU/script/
  physics/resource data from the runtime profiler contract.
- [ ] Add searchable diagnostics and logs with stable source/entity/field links.
- [ ] Add input, renderer, physics, navigation, asset residency, and network
  debug views backed by real service data.
- [ ] Add explicit dirty-document close handling and recovery for interrupted
  saves; cached recovery data must never silently become authored source.
- [ ] Persist only workspace layout/preferences as editor state.
- [ ] Add end-to-end authoring tests: create project, edit hierarchy/components,
  undo/redo, save, validate, play, cook, and package.
- [ ] Verify keyboard and pointer flows, readable error/empty/disabled states,
  narrow-window layout, high DPI, and repeated renderer/runtime lifetimes.

**Gate:** a small 2D or lightweight 3D game can be assembled, inspected, played,
diagnosed, cooked, and packaged without manual JSON editing, while direct text
editing and conflict handling remain supported.

### Immediate next task

Start with **Milestone 3: stable-ID picking and transform gizmos**. Milestone 2
now provides the independent camera, focused viewport input, local/world mode,
and snap-setting owner that picking and gizmos must reuse.

For each todo item that mutates authored state, use this implementation order:

1. add the UI-free command/model behavior and focused tests;
2. expose one narrow operation through `EditorWorkspace`;
3. wire the panel interaction and visible failure state;
4. update the preview only after the authored command succeeds;
5. run build, focused tests, affected project validation, and relevant smoke;
6. update the checkbox and current-slice description in this document.

## Boundaries

`EditorSceneJson` owns pure authored-scene JSON manipulation (entity lookup,
subtree collection, id generation, parent remapping). `EditorSceneCommand`
owns the reversible value-type command variant and its apply/revert.
`EditorSceneDocument` owns the authored JSON and command history and stages
each structural mutation through the shared `validateSceneDocument` before
committing. `EditorDocumentStore` owns conflict detection and atomic file
replacement. `EditorWorkspace` coordinates those documents with loaded project
state, selection, source discovery, and diagnostics, and rebuilds its preview
world through `loadSceneDocument` after structural changes.
`EditorHierarchyPanel` owns hierarchy filtering, menus, and drag/drop intent;
it submits only stable IDs to `EditorWorkspace`. `EditorUiHost` owns SDL3,
bgfx, input forwarding, the authored 3D viewport, and the Dear ImGui frame
lifecycle. The viewport reuses `BgfxRenderer3D`, `GpuResources`, and
`RenderCommands` on a separate bgfx view. Runtime and authored-data modules do
not depend on ImGui.
