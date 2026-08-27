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
  authored 2D, 3D, and isometric transform parents. The scene's runtime HUD is
  also projected as a distinct nested `HUD` subtree with UI-specific icons and
  visibility state; selecting a HUD node shows its resolved properties, while
  prefab-expanded nodes remain visibly read-only.
- The authored HUD is rendered through the runtime UI renderer over both 2D
  and 3D scene views. Visible elements can be picked, moved, and resized on the
  canvas; empty transparent containers do not steal scene selection. The same
  selection drives rect-transform, anchor, visibility, text, texture, color,
  delete, Undo/Redo, and Save controls in the Inspector.
  Canvas clip rectangles remain viewport-local and are translated by the
  renderer for embedded backbuffer regions, so glyph atlases and clipped icons
  render at the same positions as their solid UI geometry.
- `+ UI Element` creates containers, panels, labels, buttons, images, and text
  inputs under the selected authored HUD node. The operation is reversible and
  updates the viewport and hierarchy immediately without writing the file
  until Save.
- Selection drives a reflected inspector over the authored scene document.
  Boolean, integer, number, string, enum, vector, and color fields are editable
  using shared component descriptors, including their numeric bounds. Numeric
  fields accept direct keyboard input rather than requiring pointer drags.
- Entity names, explicit enabled state, and explicit layers are editable.
- Components can be added from the descriptor catalog and removed from the
  inspector; both are staged, validated against the shared scene validator, and
  undoable. Add Component opens with a keyboard-focused search that matches
  native and Lua components by display name, internal name/module, and category;
  empty categories disappear and unmatched searches show an explicit state.
- Lua files annotated with `@demi_component` are discovered under Add Component
  using their optional display name, category, and description. Annotated table
  fields provide inferred defaults and typed Inspector controls while scenes
  continue to store the normal `LuaScript` component.
- The hierarchy supports create, duplicate (with child subtree and stable-id
  remap), drag-and-drop reparenting to another entity or the scene root, and
  atomic subtree deletion through real, reversible commands.
- Inspector changes are reversible commands. Continuous edits collapse into a
  single undo step, and optional fields can be explicitly authored or reset
  without losing their original presence through Undo. Save writes
  deterministic JSON through same-directory atomic replacement.
- When the scene changes externally, a modal offers Reload from disk, Keep
  editing, Save Copy, and Cancel. The editor never overwrites the external
  version, and a failed preview rebuild restores the document and both history
  stacks.
- The Assets panel presents authored project files in a folder tree and compact
  file grid while excluding generated, build, package-cache, and Git internals.
  Generic file glyphs remain intentionally honest until preview generation is
  implemented by the relevant specialized-document preview milestone.
- Asset workflows are operational: type filtering, manifest metadata,
  dependencies, source/import diagnostics, Linux cook freshness, reimport,
  filesystem location, importer-backed import, and versioned asset-group
  creation/editing all use authored files and shared engine services.
- Desktop files can be dropped onto the editor to queue the same importer
  workflow used by `+ Import`; stable IDs remain editable and multiple dropped
  files are processed in order. Directories and unsupported formats use the
  normal importer diagnostics.
- Project Settings edits preload assets/groups and scene membership through a
  conflict-safe reversible project document. New Project uses the existing
  template catalog and atomic scaffolder.
- Validate, transactional Linux Cook, Linux Package, and supported Android
  Package run as cancellable background operations with structured progress
  and diagnostics. The CLI and editor share the same packaging service, and
  Build Project is enabled only when at least one real target is selected.
- The shared shell now follows the Minimal Voxel visual contract: grouped icon
  commands, central Viewport/Game View tabs, quiet charcoal panel chrome,
  compact Inspector sections, hierarchy visibility controls, a dedicated
  Build panel, and split engine/project status.
- The Console displays diagnostics from the same `validatePath` service used by
  the CLI. Rejected authored edits also appear beside their Inspector target
  and in the Console through one document issue.
- Console diagnostics and build results share searchable severity filters,
  copyable source locations, and stable entity/component/field navigation when
  the validator exposes that context.
- Embedded Play enables the runtime profiler and the Profiler tab presents
  real latest/average/p95/max CPU scope timings plus renderer submissions,
  physics, Lua, animation, network, input, world/UI counts, and asset residency.
  GPU time is shown as unavailable rather than estimated because the bgfx
  backend does not yet expose timestamp-query results.
- The Debug tab reads one immutable embedded-runtime snapshot for input,
  renderer gauges, 2D/3D physics bodies and contacts, NavigationGrid2D state,
  resident asset ownership/bytes, and network mode/security/latency. Runtime
  overlay toggles affect only the isolated Play world and are discarded on
  Stop.
- The editor exposes draw order as a selection-scoped inspection tool: select
  an entity in the runtime hierarchy, then enable `Selected draw index`. Only
  that entity receives a backed callout and leader line. Entity ID is already
  present in the runtime Inspector and is deliberately not duplicated in the
  viewport. The CLI's global `entity_ids` overlay remains available.
- Play saves pending valid changes and starts an isolated embedded runtime
  world in the Game view. Pause/Resume, exact fixed-tick Step, and Stop control
  that world; an owned external `demi-runtime` window remains available from
  the transport options.
- The central scene view renders authored 2D and 3D entities through the
  engine's existing bgfx renderers on the editor graphics device. It does not
  maintain a second editor-only rendering implementation.
- Deterministic CPU picking selects authored entities by stable ID and keeps
  viewport, hierarchy, and Inspector selection synchronized, including empty
  space and scene reloads.
- Move, rotate, and scale gizmos submit parent-aware Transform3D edits through
  the existing document command path. Local/world mode and position, angle,
  and scale snapping are available in the viewport toolbar. Holding `Shift`
  during a drag temporarily bypasses snapping; Escape or focus loss cancels a
  drag without leaving an undo entry. Gizmos and the orientation indicator use
  the renderer's right-handed camera basis.
- Bounds, collider, light, and camera overlays are extracted by the shared
  runtime debug-geometry path and can be toggled from the viewport.

Lua-created/runtime-generated geometry appears only in the Game view or the
optional external Play window. The Game view uses its own renderer and GPU
target, focused gameplay input, and read-only runtime hierarchy/Inspector;
runtime mutations are discarded on Stop. Builds remain disabled until their
real engine services exist.

## Visual design target

The polished editor reference supplied for the Minimal Voxel project is the
authoritative visual target. The current Dear ImGui screen is a functional
authoring scaffold, not an accepted approximation of that design. Preserving
the same rough panel positions is insufficient.

The target is a compact native game-development workspace with three clearly
layered horizontal bands: the application menu, a grouped icon command bar,
and the document workspace. It uses charcoal surfaces with small depth changes,
fine separators, restrained violet selection, and tightly aligned controls.
Text labels are reserved for menus, panel titles, properties, and actions that
would be ambiguous as icons.

Required layout and presentation:

- The top command bar groups file/history, Play controls, viewport tools,
  snapping, visibility, configuration, and settings. Groups have separators
  and consistent square hit targets; the current row of unrelated text buttons
  is temporary. Undo and Redo use conventional left- and right-facing hooked
  arrows so their direction is recognizable without a label.
- Scene and Game are document tabs directly above the central canvas. Switching
  views is not presented as a small global-toolbar text button.
- Hierarchy rows communicate nesting, entity kind, expansion, visibility, and
  locked/runtime-owned state without turning generated data such as grid cells
  into ordinary authored entities.
- The Inspector has an entity header followed by visually distinct component
  sections. Labels and editors form a stable property grid; vector axes, color
  channels, reset actions, pickers, and resource fields must remain compact and
  keyboard operable.
- The lower workspace uses tabbed Console/Output/Profiler and
  Assets/Lua Console regions. Tabs must not imply functionality that has not
  been connected to a real service.
- The Assets region is ultimately a folder tree plus breadcrumb/search toolbar
  and thumbnail/file grid. The current flat source list remains honest but is
  not the final asset-browser design.
- Build targets remain a narrow dedicated panel with configuration controls and
  one clear primary build action once the real build service is connected.
- The status bar keeps engine/language context on the left and project, target,
  validation/runtime state, and readiness on the right.

The signature visual element is the central stage: Scene/Game tabs and compact
viewport tools frame an uninterrupted render surface, while surrounding panels
remain quieter and denser. Do not compensate for missing functionality with
decorative cards, oversized typography, invented data, or permanently disabled
controls.

The shared visual-alignment checkpoint is implemented in `EditorTheme`,
`EditorPanelStyle`, `EditorChrome`, `EditorToolbar`, `EditorAssetsPanel`, and
`EditorShell`. Asset thumbnails, real profiler content, Lua Console behavior,
and functional build controls still belong to their owning later milestones.

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
reset actions. Wheel input remains a floating-point per-frame delta: it zooms
the hovered Scene view or scrolls the hovered editor panel, including on
high-resolution touchpads.

### Milestone 3 — Implement stable-ID picking and transform gizmos

**Depends on:** Milestones 1 and 2.

**Goal:** select and position authored entities directly in the Scene view while
keeping all mutations in the existing command path.

- [x] Add picking through an ID buffer or deterministic CPU query that returns a
  stable entity ID. Never store editor IDs in authored components.
- [x] Keep hierarchy and viewport selection synchronized through
  `EditorWorkspace`, including empty space, deleted entities, and reloads.
- [x] Render translate, rotate, and scale gizmos for Transform3D components in
  the current 3D Scene view. Transform2D gizmos belong with the future 2D
  preview rather than a parallel ImGui renderer.
- [x] Support local/world modes and the toolbar's position/angle/scale snapping,
  with `Shift` as a temporary snap bypass.
- [x] Convert world-space gizmo results to authored local transforms through the
  existing parent hierarchy helpers.
- [x] Coalesce one pointer drag into one undo transaction and cancel it cleanly
  on Escape, focus loss, validation failure, or target deletion.
- [x] Add runtime-backed bounds, collider, light, and camera overlays; debug
  visuals must match the runtime shapes.

**Gate: passed.** Deterministic picking remains stable across duplicate,
delete, empty selection, and reload cases. A multi-update drag creates one undo
transaction, `apply -> undo` restores canonical JSON exactly, and cancellation
also restores the pre-drag redo branch. Focused tests cover snapped
move/rotate/scale output, Escape and focus-loss cancellation, deleted targets,
rotated parents, non-uniform parent scale, and shared runtime overlay geometry.

### Milestone 4 — Complete inspector and hierarchy usability

**Depends on:** Milestone 1; reference picking may reuse Milestone 3 selection.

**Goal:** make ordinary component authoring discoverable without adding
component-specific branches to the generic inspector.

- [x] Extend shared field metadata with editor label/help, reference kind,
  canonical default, read-only policy, restart requirement, and useful numeric
  step where those values are missing.
- [x] Add asset/entity/prefab reference pickers backed by the shared resolver.
- [x] Filter and group Add Component choices by metadata category/domain; show
  why an incompatible component cannot be added.
- [x] Add inline validation messages and tooltips without hiding the full
  diagnostic from the Console.
- [x] Add explicit multi-selection with mixed-value display and one atomic
  multi-target command for fields common to every selection.
- [x] Add keyboard hierarchy actions for rename, duplicate, delete, focus, and
  create-child while respecting text input focus.
- [x] Keep raw arrays/objects read-only until a dedicated reversible collection
  editor exists.

**Gate: passed.** The generic inspector remains descriptor-driven with no
component-name switch. Reference choices come from the asset registry, authored
entity IDs, and prefab resolver. Focused model/document tests cover mixed common
fields, domain incompatibility, shared defaults, and atomic multi-edit rejection
and undo.

### Milestone 5 — Add the authored 2D Scene view

**Depends on:** viewport input and stable-ID selection from Milestones 2 and 3.

**Goal:** provide equivalent scene authoring for 2D projects through the normal
2D renderer.

- [x] Detect whether the active scene is 2D, 3D, or mixed without storing an
  editor-only project mode.
- [x] Render authored 2D content through `BgfxRenderer2D`, the normal asset
  registry, camera extraction, layer ordering, and parent transforms.
- [x] Add a 2D editor camera with pan/zoom, pixel-aware grid, and frame-selected.
- [x] Reuse stable-ID picking, selection, snapping, and command transactions.
- [x] Add sprite/tilemap/collider/camera bounds overlays from runtime extraction.
- [x] Define an explicit view switch for mixed scenes; never approximate sprites
  with ImGui primitives.

**Gate: passed.** Pure 2D scenes select the authored 2D view automatically;
mixed scenes expose an explicit transient 2D/3D switch. `BgfxRenderer2D` draws
the authored world in an editor viewport region with the normal asset registry,
ordering, and parent transforms. Runtime collider primitives are reused for the
collider overlay, while the UI-free overlay extractor resolves sprite, tilemap,
and camera bounds from runtime world queries and loaded tilemap assets. Focused
coverage verifies domain detection, pan/zoom, projection, picking, gizmo edits,
and exact authored-JSON restoration through undo. The production and networking
2D projects validate, and the production project passes a headless runtime
smoke.

Isometric sprites use the renderer's placement, pivot, bounds, depth, layer,
and sorting rules for selection. `IsoTransform` entities participate in normal
hierarchy parenting and expose tile-axis move gizmos. Compact per-cell grid
texture maps are projected as virtual `Painted Cells` below their grid entity;
selecting one exposes its coordinate and texture through a dedicated inspector
and lets the tile-axis gizmo move it without expanding the authored scene into
one entity per cell. The generic inspector never exposes the map as raw JSON.

2D controls: middle mouse pans, the wheel zooms, `F` frames the selected
entity, and `Shift` temporarily bypasses transform snapping. Move, rotate,
scale, local/world mode, grid, bounds, colliders, and camera overlays use the
same Scene view toolbar and selection as 3D.

### Milestone 6 — Add an embedded Game view and deterministic Play mode

**Depends on:** stable document safety and viewport ownership.

**Goal:** run the game inside a separate runtime world without contaminating the
authored preview. Keep the current external-process Play path working until the
embedded path has equivalent lifecycle coverage.

- [x] Introduce a narrow runtime-host boundary only when both external and
  embedded implementations exist.
- [x] Create a separate runtime world/render target with normal Lua lifecycle,
  fixed updates, physics, scene transitions, assets, audio, and networking.
- [x] Implement the explicit `Stopped -> Starting -> Running <-> Paused ->
  Stopped` state machine and visible failure state.
- [x] Route gameplay input only while the Game view is focused.
- [x] Implement deterministic Step as exactly one fixed tick while paused.
- [x] Expose runtime entities/components read-only in the inspector.
- [x] Stop with complete script, scene, GPU/audio, input, and network teardown.
- [x] Never copy runtime mutations into authored source automatically.

**Gate: passed.** Embedded Play owns a freshly loaded runtime world and the
normal Lua, fixed-update, physics, scene-flow, asset, audio/media, networking,
and accessibility services. Its Game renderer owns a resizable offscreen GPU
target that is released on Stop. Three repeated lifecycle cycles return the
session owner count to baseline; paused Update advances no fixed ticks, Step
advances exactly one, startup failure becomes a visible `Failed` state, and the
authored scene remains byte-for-byte unchanged. Scene-transition failures are
contained by the same runtime-session failure boundary.

### Milestone 7 — Make assets and project workflows operational

**Depends on:** Milestone 1 for document safety. Cooking/building can progress
independently of viewport gizmos.

- [x] Extract `EditorAssetsPanel` when adding state beyond the current filter and
  selection; keep source discovery in `EditorWorkspace` or a named source index.
- [x] Add folder navigation, type filters, asset-manifest details, dependency and
  stale-cook status, and locate-in-filesystem.
- [x] Add import/create actions through existing importer and template services;
  never write an editor-only asset database.
- [x] Edit project preload `assets`, asset groups, and scene membership through
  versioned authored documents and reversible commands.
- [x] Add project creation through existing project templates.
- [x] Connect Validate, Cook, Linux Package, and supported Android packaging to
  the existing service layer with structured progress, cancellation, and
  diagnostics. Do not parse human CLI output when a service API exists.
- [x] Enable Build Project only after selected targets and configuration produce
  real artifacts and failures remain visible.

**Gate: passed.** Editor-triggered validate/cook/package produces the same diagnostics,
manifest, and artifacts as the corresponding CLI service; cancellation leaves
no committed staging directory, half-owned child process, or false success
state. UI-free coverage exercises project and asset-group undo/save, importer
and reimport flows, transactional cook/package parity, replacement, and
cancellation. Linux and Android CLI package smokes exercise the shared service.

### Milestone 8 — Add prefab, HUD, and specialized document editors

**Depends on:** Milestone 1 and the relevant inspector/viewport foundations.
Implement one real document type at a time; do not introduce speculative base
classes before a second format needs shared behavior.

- [x] Prefab editor: source/expanded view, nested stable IDs, override diff,
  apply/revert, missing references, and atomic multi-file failure handling.
- [x] HUD editor: hierarchy, anchors/layout, safe-area, DPI, locale, and sample
  data through the runtime layout engine without saving generated nodes. The
  active scene HUD is also a first-class viewport document with visual
  selection, move/resize handles, typed Inspector controls, and structural
  add/delete commands; raw JSON is an advanced fallback rather than the main
  workflow.
- [x] Material editor: reflected properties and runtime-backed preview.
- [x] Animation editor: clip/state-machine editing and preview using existing
  animation assets and runtime playback rules.
- [x] Data/dialogue, input-action, and audio editors as adapters over their
  versioned authored documents and shared validators.

**Gate: passed.** Each specialized editor saves deterministic canonical source
through conflict-aware documents and has no second unsynchronized authored
model. Prefab apply rolls back the referenced source if the owner commit
conflicts; HUD previews are runtime-layout projections; material, animation,
data/schema, input, and audio changes use the same parsers and validators used
outside the editor.

### Milestone 9 — Diagnostics, profiler, recovery, and release gate

**Depends on:** the services being displayed; it can be delivered incrementally
as those services gain structured data.

- [ ] Replace placeholder profiler content with real frame/CPU/GPU/script/
  physics/resource data from the runtime profiler contract.
- [ ] Add searchable diagnostics and logs with stable source/entity/field links.
- [x] Add input, renderer, physics, navigation, asset residency, and network
  debug views backed by real service data.
- [x] Add explicit dirty-document close handling and recovery for interrupted
  saves; cached recovery data must never silently become authored source.
- [x] Persist only workspace layout/preferences as editor state.
- [x] Add end-to-end authoring tests: create project, edit hierarchy/components,
  undo/redo, save, validate, play, cook, and package.
- [x] Verify keyboard and pointer flows, readable error/empty/disabled states,
  narrow-window layout, high DPI, and repeated renderer/runtime lifetimes.

**9A present:** the placeholder Profiler has been replaced with runtime-backed
rolling samples and searchable category tables. Console and build diagnostics
are searchable by severity, code, message, path, entity, component, and field,
with source-copy and entity-selection actions. GPU timestamps, runtime log
ingestion, debug views, recovery/preferences, and the full release workflow
remain open, so the Milestone 9 gate is not yet marked passed.

**9B present:** a categorized Debug tab consumes `RuntimeDebugSnapshot` for
input, physics/contact counts, navigation configuration, resident asset memory,
and network state, plus the profiler's renderer gauges. Collider, contact,
selected draw-order, and UI-bound overlays mutate only the embedded
runtime world. No authored debug configuration is changed.

**9C present:** scene, project, HUD, and active specialized-document changes
are mirrored into an atomic recovery cache under the user's cache directory.
On restart, Restore loads validated content as dirty memory state and Discard
removes the cache; neither action silently writes authored source. Menu and OS
window close share an explicit Save All / Discard / Cancel modal. Snap values
and viewport visibility preferences are the only persisted editor state and
live under the user data directory, never in the project.

**9D present:** one deterministic release workflow scaffolds Blank 2D, authors
hierarchy and component changes through `EditorWorkspace`, exercises Undo/Redo,
saves, validates, runs embedded Play, cooks Linux content, and packages the real
runtime. Responsive layout calculations are shared by the Shell and tested down
to 320x240 without panel overlap; DPI font policy is tested at low, standard,
and 2x scale. Pointer/keyboard regression suites, three embedded Play lifetimes,
and three complete bgfx device lifetimes cover repeated interaction and teardown.

**Gate:** a small 2D or lightweight 3D game can be assembled, inspected, played,
diagnosed, cooked, and packaged without manual JSON editing, while direct text
editing and conflict handling remain supported.

### Immediate next task

Milestone 9A through 9D are present. Continue with **Milestone 9E: structured
runtime log ingestion and real GPU timestamps where supported**. Do not mark
the profiler/diagnostics bullets complete while either remains unavailable.

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
it submits only stable IDs to `EditorWorkspace`. HUD rows are a read-only
hierarchy projection of the parsed `UiDocument`, not synthetic scene entities;
HUD mutations remain owned by the specialized document editor.
`EditorUiHost` owns SDL3, bgfx, input forwarding, the authored 3D viewport, and
the Dear ImGui frame lifecycle. The viewport reuses `BgfxRenderer3D`,
`GpuResources`, and `RenderCommands` on a separate bgfx view. Runtime and
authored-data modules do not depend on ImGui.

Milestone 7 keeps authored and operational responsibilities separate:
`EditorProjectDocument` and `EditorAssetGroupDocument` own reversible,
conflict-safe JSON; `EditorAssetIndex` is a rebuildable read-only projection;
`EditorWorkspaceAssets` coordinates importer and authored-file actions;
`BuildService` is the synchronous structured CLI/editor contract;
`EditorProjectOperations` owns its background thread and cancellation; and
`EditorAssetDialogs`, `EditorBuildPanel`, and `EditorProjectPanel` own only
presentation state. No editor-only asset or project database exists.

Milestone 8 adds `EditorJsonDocument` only for persistence/history shared by
the now-proven specialized formats. `EditorSpecializedDocument` selects the
real prefab, HUD, material, animation, data, and audio validator;
`EditorSpecializedPanel` owns source/preview presentation; and
`EditorAnimationMachinePanel` submits state-machine changes through the normal
scene command path. SDL file drops cross `PlatformHost` and `EditorUiHost` as
paths, then enter `EditorAssetDialogs`; they never bypass `AssetImporter`.
`EditorHudDocument` owns nested authored-node operations and HUD history;
`EditorHudCanvas` owns screen-independent bounds and picking; the hierarchy,
viewport, and Inspector consume those services through `EditorWorkspace`.

Milestone 9A keeps profiling and diagnostics UI-free until presentation:
`RuntimeProfiler` owns bounded rolling samples; `EditorProfilerModel`
categorizes immutable snapshots; `EditorDiagnosticsModel` merges and filters
CLI/editor records; and `EditorConsolePanel` owns only filter and tab state.
`EmbeddedRuntimeSession` owns `RuntimeDebugSnapshot`; `EditorDebugPanel`
renders that value and can submit only runtime overlay configuration.
`EditorRecoveryStore` owns cache-only atomic snapshots keyed by project path;
`EditorPreferencesStore` owns only editor presentation settings. Restoration
enters normal dirty document state through `EditorWorkspace` validation.
`EditorWorkspaceLayout` is the responsive Shell geometry and DPI policy; the
release-workflow test composes public scaffold, workspace, Play, BuildService,
Cook, and Package contracts without ImGui-only shortcuts.
`DebugLabelLayout2D` owns debug-callout compaction and placement independently
of bgfx; `DebugCanvasRenderer` only measures and draws the resulting callouts.
