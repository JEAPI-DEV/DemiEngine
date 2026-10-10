# Editor commands, clipboard and docking qualification

## Ownership

`EditorCommands` defines action identities, default bindings and contexts.
`EditorKeyBindings` parses and validates per-user overrides, including conflict
checks and the required Game View cursor-release binding. `EditorShortcutInput`
adapts ImGui key events; `EditorShellCommands` routes them to the focused view.
Text input, active gestures, dialogs and key recording do not execute authoring
shortcuts. Game input has a separate context.

Menus, tooltips and toolbar actions use the same registry and execution paths.
Preferences persist below the platform user-data directory, outside projects and
packages. The settings panel supports replacement, alternatives, clearing
optional bindings and reset. Recorded bindings are validated before publication.

`EditorClipboard` owns the versioned typed system-clipboard envelope.
`EditorAuthoringClipboard` remaps durable IDs and typed references without
rewriting gameplay strings or resource URIs. Scene/HUD documents own atomic
paste, duplicate and multi-delete commands. Their workspace adapters preserve
selection and preview rollback. Scene and HUD selection support multiple objects
and omit selected descendants already covered by a copied parent. Prefab sources
are editable; copied instances retain their URI and overrides.

Graph commands copy selected nodes and their internal links, allocate fresh IDs
on paste and preserve parameters and logical positions. Clipboard changes and
multi-node movement use one draft-history entry. History restoration invalidates
the display-position cache so Redo cannot reuse freed ImNodes coordinates. Cut
checks system clipboard publication before deleting authored content.

The action icon set extends the existing native vector glyph renderer; it does
not require an icon font or generated raster assets. Property labels and action
tooltips remain available.

## Verification (2026-10-05)

- Release editor and affected test targets built successfully.
- Seven selected CTest checks passed in 2.79 seconds: authoring clipboard,
  recovery/preferences, graph UI, graph document, workspace, drag authoring and
  game authoring. This is a scoped gate, not a full repository test run.
- Coverage includes scene/prefab and HUD/UI-prefab transfers, fresh-ID collisions,
  typed internal references, multi-selection, atomic Undo/Redo, malformed payload
  rejection, failed clipboard writes, graph zoom preservation, scoped Ctrl+D,
  custom key capture and persisted key mappings.
- `git diff --check` is clean. UI checks use synthetic ImGui input; physical
  desktop interaction and Android qualification were not run for this change.
- PHP is unavailable locally, so website template rendering was not tested.

Clipboard commands operate on authored selections, not arbitrary file/folder
operations in the Assets browser. Unannotated gameplay strings remain opaque;
they are not guessed to be entity references.

## Independent docking and contextual palettes (2026-10-05)

`EditorPanelDefinition` is the shared UI-free inventory of window identities,
visibility keys, default dock groups and palette availability. The visibility
store, View menu and default dock builder consume this inventory. The 15 windows
are Viewport, Prefab, HUD, Terrain Graph, Terrain Asset, Game View, Inspector,
UI Palette, Terrain Nodes, Console, Lua Console, Profiler, Debug, Hierarchy and
Assets. Authoring views, property tools and diagnostic tools start as ordinary
ImGui dock tabs; each window can move or close without changing its siblings.

Inspector no longer embeds a tab bar or palette. `drawEditorPalettePanel` receives
an explicit module kind, rather than inferring the palette from the current
document. HUD contexts include controls and UI prefabs; terrain contexts include
only terrain nodes. Shell owns contextual availability: UI Palette is submitted
only during HUD/UI-prefab editing, and Terrain Nodes during graph editing.
Suppressing an unavailable panel leaves its per-user visibility preference and
dock placement intact.

Code inspection of the integration confirms distinct Scene, Prefab, TerrainAsset
and Hud sessions. The HUD attached to a scene is opened into the independent Hud
session when editing begins; explicit HUD/UI-prefab sources use the same path.
Each session owns its source state, selection and history. Focus selects which
session Inspector and Hierarchy present without replacing another view's source.
Save-all preflights duplicate dirty source ownership before writing, and recovery
records preserve session identity.

Palette View-menu availability uses retained authoring flags, rather than the
currently focused command context. Inspector/palette focus therefore keeps the
active palette available. Terrain Nodes dispatches graph commands; runtime
Inspector/Hierarchy dispatch no authoring commands. These rules are independent
of each panel's saved visibility preference.

Opening Terrain Graph calls `pinTerrainAuthoring` with its stable owner ID and
source document. Authoring synchronization uses that target instead of Inspector
selection. The graph no longer forces selection back to its terrain or stops
drawing when selection changes. Brushes still require the bound terrain to be
selected. Retargeting rejects an unapplied draft or pending terrain work; hiding
the window does not implicitly release its target.

The independent layout uses `workspace/docking-layout-v2.ini` and
`workspace/panels-v2.json` with visibility format version 2. Previous v1 layout
and visibility files are preserved but are not read as aliases. Corrupt v2 layout
recovery still quarantines the invalid file rather than deleting it. Reset
restores the v2 default groups and visibility preferences.

Added coverage:

- Docking state: all 15 preferences round-trip independently, keys/window names
  are unique, invalid state restores defaults without partial application, and
  v1 files remain unchanged.
- Synthetic docking UI: distinct window IDs and default group DockIds, moving
  Terrain Graph and UI Palette without their siblings, independent closing,
  workspace reset, explicit palette kind filtering and contextual hiding.

Verification of the integrated change:

- Release editor, CLI and affected test targets built successfully.
- Seventeen selected CTest checks passed in 4.12 seconds: docking state/UI,
  console docking, render-view ranges, document sessions, Shell docking,
  authoring clipboard, graph UI/document, drag authoring, workspace,
  recovery/preferences, prefab components, game authoring and terrain
  asset/history/workspace. This is a scoped gate, not a full repository run.
- Session checks cover independent worlds and histories, conflict-aware Save
  All, transactional recovery, linked HUD cache refresh and creating registered
  scenes while editing HUDs or prefabs without replacing their previews.
- Shell checks cover simultaneous Scene/HUD render requests, retained HUD
  identity and selection, hiding/reopening dock tabs, contextual palette focus,
  the idle Game View and Delete from read-only runtime Inspector/Hierarchy.
- Terrain checks cover pinned graph ownership, generation commits after
  selection changes, brush routing and preserving unapplied drafts when
  retargeting or switching documents.
- `minimal_3d` and `terrain_graph_3d` validate without diagnostics. Native
  SDL3/Vulkan startup smoke checks use isolated per-user directories and twelve
  frames for the main scene, prefab source and HUD source.
- `git diff --check` is clean. Interactive desktop dragging and Android
  qualification were not run; docking gestures are covered by synthetic ImGui
  input. Website templates were not rendered or deployed.

## Palette cards and terrain drop delivery (2026-10-05)

Docked UI/Terrain palettes and the optional local terrain palette share one
card renderer: a full-card interaction target with an icon, wrapped title and
description. Terrain drop acceptance runs inside the imnodes canvas child and
uses its explicit bounds; it no longer depends on the parent group's last item.
Delivery adds one node after the existing canvas interaction pass, preserving
selection and the single insertion Undo step.

The Release editor rebuilt successfully. Three focused checks passed in 0.23
seconds: graph UI, drag authoring and Shell docking. The new graph check uses
mouse press/movement/release from a card's description to the canvas, rejects
mutation during hover, verifies one inserted node and verifies Undo. Existing
HUD drop coverage remains green. Native visual inspection was not performed for
this follow-up; website guidance was updated locally, not deployed.

## Terrain controls, captures and display-sized startup (2026-10-05)

Input source dropdowns use `EditorTerrainGraphDocument::setInputSource` for
transactional rewire/disconnect, typed-port validation, cycle rejection and one
Undo step. Nodes distinguish Inputs, Parameters and Outputs; Terrain Output
exposes active-output selection. Node-context sheets reuse the same landscape
settings as the root sheet, including Hex RGBA and normalized float precision.
Viewing settings does not materialize defaults or quantize authored colors.

Explicit view-focus requests now finish after panel submission, so newly
appearing dock siblings cannot override a requested graph tab. The CLI supports
`--terrain-graph` and `--terrain-settings-node <id>` without modifying source.
Unprepared lazy viewport targets are skipped until ready rather than reporting
a false startup rendering failure.

The shared SDL window configuration supports optional maximized startup; its
default remains false for runtime callers. The editor requests maximization and
remains windowed/resizable. `scripts/capture_editor_window.py` defaults to the
display-sized editor, captures only its PID/title-verified window, isolates XDG
state and records provenance. Fixed capture dimensions remain optional.

Verification:

- Release editor and relevant tests rebuilt; six selected CTest checks passed
  in 0.63 seconds: graph UI/document/settings, Shell docking, drag authoring and
  SDL platform host. This is not a full repository gate.
- Settings tests exercise real Hex RGBA and precision inputs, collapsible
  sections, shared-draft rules, landform references and palette selection.
- A fresh-layout regression opens the graph before the first frame and verifies
  visible tab selection without simulated clicks or forced layout changes.
- Native SDL3/Vulkan captures confirmed maximized editor work-area dimensions
  of 5120 x 2806 on the available display. SDL's dummy-driver test checks option
  defaults and lifecycle safety, not window-manager maximization behavior.
- Real captures are published as `public/images/docs-terrain-connections.png`
  and `public/images/docs-terrain-colors.png` in the package-store source. They
  were visually inspected and linked from terrain guidance with full-size links.
- `terrain_graph_3d` validates without diagnostics; `git diff --check` is clean.
  Website templates were not rendered or deployed, and Android qualification
  was not performed for this desktop editor change.

## Primitive creation and terrain placement

The native ImGui Hierarchy separates Add Empty from visible primitive creation
and physics-only presets. Cube/Sphere/Cylinder/Plane buttons support click
creation and drag/drop into authored 3D views. New primitives author a mesh and
basic collider, not a Rigidbody. Cylinder collision uses a capsule approximation;
plane collision uses a thin box. An empty MeshRenderer uses the existing native
light-grey unit-cube defaults; no renderer defaults or artificial editor-only
geometry were added. Component help and a physics-only Inspector action explain
and expose the missing renderer.

`sceneDropWorldPosition3D` uses the immutable generated terrain surface and its
owner's hierarchy transform. It chooses the closest hit on enabled terrain,
then the ground plane within the view's working range, then the cursor ray at
the view focus distance. There is no fixed world-distance limit on terrain hits.
Primitive feet are offset above the hit; prefabs retain authored pivot/root
offsets. Independent props do not automatically follow later terrain changes.
Precise imported mesh surface placement remains outside this change.

The 3D gizmo captures its projected world-length/pixel-length ratio at drag
start. Translation uses that ratio; scale additionally divides by object bounds
and parent scale. Rotation remains angular. Tests compare perspective drags at
10 and 1000 world units, orthographic distance independence, scaled mesh sizes,
snapping and captured drag ownership.

The scene command gate also exposed duplication retaining flat internal parent
links in new copies. `nestLocalEntityLinks` nests only those copied local links,
including forward references; external parents and the original source remain
unchanged. Existing nested clipboard layout and native entity-reference remapping
remain tested.

Release editor/CLI builds and nine focused checks pass (3.57 seconds): viewport
tools, drag authoring, scene commands, game authoring, workspace, Inspector model,
2D scene view, shell docking and authored clipboard. Elevated translated terrain,
disabled terrain, parent-local placement and one-command Undo receive regression
coverage. `terrain_graph_3d` and a small isolated placement fixture validate
without diagnostics. A PID/title-verified native Vulkan capture at 5120×2806
visually confirms the default cube on elevated terrain; it is published as
`public/images/docs-editor-prop-placement.png`. These checks do not claim manual
qualification of every drag gesture, imported art, Android or the full test suite.
Website templates were updated but not rendered or deployed. User scene edits
and their existing prop positions were left untouched; `git diff --check` is clean.

## Hierarchy-to-Inspector references

Native entity-reference fields and annotated Lua `entity` properties share the
reference presentation adapter. Drops accept only a null-terminated stable ID
from the active choice set, including resolved prefab members. Assignment uses
the existing document commands; the UI does not retain native object pointers.
Hierarchy entity selection happens on click release below the drag threshold,
so starting a drag does not replace the Inspector's target. Empty references use
the existing empty-string property contract. Scripts must check target lifetime.

The focused Inspector model, ImGui input, prefab components, Lua component
metadata, Lua scripting and property-contract gates cover payload validation,
actual ImGui drop delivery, wrong payload rejection, annotation defaults,
Undo/Redo, Save/reload and passing the assigned ID to Lua entity services.
No new public Lua service or generated stub is needed. Website property and
workspace guides describe assignment; this change does not claim desktop
gesture qualification, prefab-local script-reference rebasing, or automatic
reference repair when an object is destroyed.

The first qualification missed the Inspector's live-world choice construction:
it tested the control with manually supplied choices. That construction wrongly
excluded ordinary entities because `editorPlacementOwner` returns an empty
string for them, not their own ID. Choice construction now lives in the UI-free
Inspector model, excludes only a nonempty different generated owner, and has
ordinary-entity, prefab-member and generated-water regressions. The prefab
components gate also performs a real hierarchy press/drag/release into the
actual annotated property row, checks selection retention, and verifies the
assigned ID through Undo/Redo and save/reload.

Hierarchy rename qualification now exercises real row selection, name-field
editing, mouse submission and Enter submission, including repeated renames,
Undo/Redo and save/reload. The prior per-frame name-field focus request steals
focus from the Rename button; restoring it reproduces the failing mouse test.
Focus is now requested only when the dialog appears. Failed commands leave the
dialog open, and inherited entities initialize the field from their effective
display name. Renaming changes the display name, not stable IDs or script
references.


## Terrain preset introduction (2026-10-06)

Terrain Presets is an independent, contextual Properties dock alongside Inspector
and Terrain Nodes. The four built-in starter landscapes are shared by the editor
and CLI; project DataAsset presets remain available. Applying a preset uses the
existing generation/history boundary and confirms replacement of graph settings.
Existing sculpt and region compatibility checks still apply. Saved layouts acquire
the new tab without a workspace reset.

Graph comment nodes use the native graph registry and graph document commands.
Their text and positions round-trip, but comments have no ports and are excluded
from graph execution and generation cache identity. Preset documents may contain
the same versioned graph as terrain recipes. Starter comments explain shape,
appearance and output; the panel owns presentation, while the native preset library
owns starter data and shared loading.

Release checks cover terrain graph/preset/CLI, editor graph document/UI/settings,
terrain authoring/workspace/assets, docking state/UI/shell, and authoring clipboard.
The native Vulkan desktop probe applied Desert Dunes from the new tab, displayed
its introductory graph, and confirmed the orange terrain preview. A graph UI
regression bounds comment-card width; asset tests cover comment-only generation,
preset Undo/Redo and saving/reopening notes. The two maintained terrain examples
validate. This is focused Linux qualification, not a full-suite or Android gate.

## Prefab conversion with preserved IDs (2026-10-06)

Create prefab from selection and hierarchy-to-Assets creation now default to
replacing the selected hierarchy with a linked instance. Copy-only remains an
explicit dialog option. Instance `entity_ids` maps expanded prefab-local IDs to
stable owner-document IDs; overrides still use local keys. Nested owners apply
their outer namespace, and sibling references resolve mapped identities through
the shared composition loader. Cooked `prefab_origins` retains these mappings.

The editor records replacement as one hierarchy command, preserving source
nesting, placement and existing references. Undo restores the scene and leaves
the reusable prefab file. A failed conversion restores the scene and removes
only its unchanged newly created file. Clipboard duplication allocates fresh
mapped IDs; Inspector ownership uses the mapped origin index.

Focused Release checks cover 2D/3D conversion, transformed parents, nested
instances, source updates, overrides, Undo/Redo, copy-only, rollback, clipboard,
identity collisions, legacy/unified authoring, cached templates and fractures.
The native Linux Vulkan probe converted the colony habitat without moving it;
Inspector showed its instance origin while its ID remained `cylinder`. The
converted project validated and cooked, and its cooked scene retained that ID
and the mapped origin. This is scoped desktop qualification, not a full-suite
or Android gate.


## Align selected camera to view (2026-10-06)

The Scene menu and viewport toolbar expose selected-camera alignment. The UI
requires one matching camera and transform, and disables this mutation during
Play. The UI-free workspace operation uses shared transform hierarchy helpers
and commits differing pose/projection fields through one validated multi-field
command. Prefab cameras use the same override targets as the Inspector. Camera
scale, parent and unrelated rendering/follow settings stay unchanged.

Focused Release checks cover perspective/orthographic 3D, 2D, translated/rotated
and nonuniformly scaled parents, preserved-ID prefab cameras, singular parents,
invalid selections, transaction rollback, no-op repetition, exact Undo/Redo and
save/reopen. Native Vulkan desktop verification exercised the Scene menu action
and a single Undo on the colony camera while the editor view stayed in place.
This is scoped Linux qualification, not a full-suite or Android gate.

## HUD dock preset placement (2026-10-08)

Choosing a dock preset resets stale `position`/`at` and custom anchors in the
HUD document's existing validated replacement command. Top/Left/Fill use zero
offsets. Bottom/Right/Center derive placement from runtime-resolved dimensions
and retained margins. Authored size constraints, padding and appearance remain
unchanged. Runtime parsing of explicitly authored offsets is unchanged.

Release HUD-document, HUD-hierarchy and drag-authoring checks pass. Coverage
includes all six presets, both offset spellings, minimum sizes, margins,
reapplying the same preset, invalid choices, exact Undo/Redo and save/reopen.
A native Vulkan probe confirmed that the colony resource panel realigns from
(217.9, 79.8) to the canvas top-left with full width after selecting Top.


## HUD drops into flow layouts (2026-10-08)

HUD controls and UI prefab instances share flow-placement planning before the
existing document creation command commits. Resolved parent layout chooses
between free coordinates and Row/Column/Grid insertion order. Hidden siblings
do not consume visual slots; authored child indices and stable IDs are retained.
Prefab root position/anchors are local instance overrides, leaving the source
and inner layout intact. Menu creation appends without the default drag offset.

Focused Release checks cover row/column/grid insertions, hidden children,
padding and translated parents, prefab roots with existing position/anchors,
free placement, invalid coordinates, Undo/Redo and save/reopen. The palette drag
integration test exercises a real ImGui drop into a row. Native Vulkan testing
on a copy of the colony HUD confirmed that a new label enters the first row slot
at zero offset. Existing authored offsets are not migrated automatically.


## Hierarchy reveal and canvas captions (2026-10-08)

HUD/projection badges and selected/empty-selection captions no longer cover
HUD or scene canvases. Dock tabs, toolbar projection controls and Inspector
retain the corresponding context.

Explicit selection events, document changes and changed selected-ID lists
request a one-frame ancestor reveal. Hierarchy opens only the selected paths,
scrolls an off-screen primary selection into view without taking focus, and
clears a stale search for external selection. Re-selecting an already selected
object reveals it again; idle frames respect manual collapse. Selected scene
reparenting and HUD creation/reparenting use the same reveal path.

Release hierarchy-reveal, HUD-hierarchy, drag-authoring and shell-docking tests
pass. The UI regression covers collapsed wrappers/ancestors, same-ID reselection,
prefab children, scrolling, filters, reparenting and palette placement while
checking that reveal itself changes neither source documents nor Undo history.
Native Vulkan checks confirmed clean HUD/Viewport canvases and automatic
root/panel expansion and highlighting on HUD canvas clicks and palette drops.


## Game canvas and HUD text persistence (2026-10-08)

Game View no longer draws status badges, input hints or stopped-placeholder
text over its render target. Toolbar status and Console diagnostics remain.

HUD Inspector string fields apply validated edits on each changed value.
Shared JSON history coalesces consecutive edits for the same node/field until
Enter, deactivation, selection change, Save, Undo or Redo. Returning to the
original value drops the session entry without merging later typing into the
command beneath it. Save all is available while authoring text has focus;
other editing shortcuts remain owned by the text widget. String input widget
IDs include their workspace and node so changing selection cannot transfer
ImGui's previous edit buffer into another node.

Seven focused Release suites pass: value history, HUD documents, specialized
documents, document sessions, render views, shell docking and Play sessions.
The shell regression types across frames, saves with Ctrl+S while input is
active, reopens the source, switches directly to another label and checks one
Undo and Redo. Document tests also cover save/session boundaries and cancelling
an edit back to its original value over an unrelated command.

Native Vulkan checks on a temporary copy of the colony project confirmed
saving without Enter, saving after selecting another label, unchanged sibling
text and one-step Undo. Game View shows only the game while running and is
blank after Stop. The prototype source was kept separate from these checks.

## Automatic shared script discovery (2026-10-08)

The scene and secondary document sessions share one source index and cached Lua
annotation catalog. Script creation notifies that index immediately. Linux
native events update external script saves, atomic replacements, renames and
deletions without an F5 refresh. Inspector drawing no longer discovers/parses
all script sources. Platform event transport is isolated from editor policy;
Windows native watching is not implemented and retains manual refresh.

Release Lua metadata, document sessions, shell docking and source workflow
suites pass. Regression checks cover 100 idle polls with unchanged revision and
parse count, identical-content saves without reparsing, edits, rename/delete,
nested directory creation/moves, ignored generated paths, invalid annotation
recovery, explicit rescan, shared creation and preservation of dirty HUD source
and Undo. The overflow recovery uses the same full-rescan path; kernel queue
overflow itself was not forced in this qualification.

Native Vulkan checks on a temporary colony-project copy created a Lua script
from the HUD workspace and found it immediately in the scene's Assets list and
Add Component menu. Editing its display-name annotation outside the editor
updated the still-open component menu without F5. Test-only external-editor
preferences prevented launching another application during this check.

## Live Runtime Inspector (2026-10-09)

The Runtime Inspector uses a read-only component descriptor projection over
native runtime field bindings and component-owned wire codecs. Authored
serialization remains separate. Unsupported live fields are marked unavailable,
without falling back to cached source values; Lua instance-local variables and
unreflected simulation internals are outside this view. Field/value columns wrap
within the available Inspector width.

Component schema regression checks verify live transform and rigidbody values,
omitted defaults, successive snapshots, absent components and unchanged authored
serialization. Component schema, shell docking and Play session tests pass.
Native Vulkan testing moved the colony camera in embedded Play: the Inspector
reported its changed position while its authored scene retained the original.

## Embedded Play resource lifetime and open GPU issue (2026-10-09)

Game-renderer release after Stop or a render failure now occurs after the UI
frame is submitted. The frame may contain an ImGui Image referencing the Game
texture; queuing resource destruction before that submission violated the
intended lifetime ordering.

Play-session, render-view and shell-docking tests pass. Native testing still
reproduced an intermittent NVIDIA Vulkan crash after five Play/Stop cycles;
this change is not claimed to fix that crash. The same build survived 24 cycles
with Khronos validation enabled and 12 cycles with AMD Vulkan selected locally.
Subsequent isolation and the qualified correction are recorded in
[the Vulkan lifecycle investigation](editor-nvidia-hud-lifecycle-investigation.md).

## Headless game E2E execution (2026-10-09)

The colony resource probe exposed that headless execution loaded the E2E suite
but neither advanced its coroutine nor drained synthetic touches, then returned
after the ordinary one-frame smoke budget. Headless E2E now uses the fixed step
for both, with a default 120-second simulated budget. Explicit frame limits
remain authoritative; an interrupted suite emits one failure summary and
returns failure. A completed failing suite also returns failure directly.

The real CLI/runtime regression covers waits, scene timeout, an actual HUD toggle
touch, success/failure exit codes, explicit frame cutoff, and `demi test linux`
reporting. Headless-E2E, Lua-E2E and E2E-runner suites pass in Release.


## Vulkan program and pipeline lifecycle correction (2026-10-09)

The pinned bgfx backend now clears reused program binding metadata and scopes
its graphics/compute pipeline cache entries to their originating program.
Destroying that program evicts its pipelines through bgfx's deferred release
before retiring the pipeline layout. Unrelated live pipeline entries remain.
Both changes are maintained configure-time patches with fail-closed upstream
shape checks. They contain no OS, GPU-vendor or MSAA-off condition.

Seven focused Release suites pass. The final targeted build completed 60
full-colony NVIDIA Play/Stop/Viewport cycles without validation, 12 fresh-process
NVIDIA reproducer cycles with synchronization validation, and 12 AMD colony
cycles. Normal MSAA, threading and resource release were retained. Windows and
Android hardware qualification remains pending. The independent startup
legacy-device-layer validation warning is documented in the investigation;
it is not claimed fixed by these changes.

## Captured viewport rotation correction (2026-10-10)

Right-button rotation could stop while native relative capture was active because
ImGui received absolute cursor coordinates. The editor input adapter now maintains
a virtual pointer from SDL relative deltas during authored-view capture, then
returns to absolute coordinates on release or focus loss. This also keeps captured
pan and drag deltas independent of cursor boundaries. Game View retains its
separate exclusive input ownership. No platform-specific cursor warp was added.

The ImGui input test covers sustained motion with frozen absolute coordinates,
zero-motion frames, release and recapture. Native event tracing confirmed relative
deltas reached the viewport; the user confirmed continuous rotation in the rebuilt
editor. Temporary tracing was removed after verification.
