# Editor usability redesign

The audience is developers learning and authoring games in the native editor.
Use neutral charcoal panels, a single blue interaction accent, clear section
boundaries and consistent labelled controls. Preserve the official ImGui docking
workspace and per-user layouts. Motion should only communicate an interaction;
there is no decorative animation. The identifying pattern is a quiet workspace
with contextual actions beside the content they affect.

Implementation scope:

- Inspector: stable header, searchable declared layers/references, explained
  advanced groups, typed material overrides and one standard colour picker.
- Workspace: simplify toolbar, make creation discoverable, improve names/icons,
  group viewport display/navigation settings rather than exposing every toggle.
- Runtime actions: visible debug/release choices and bounded E2E execution with
  results, using shared runtime/CLI services behind a platform process boundary.
- Lighting: convenient basic lighting, camera and local-light creation; movable
  point/spot lights and transform-aware directional-light rotation, with actual
  renderer limits documented rather than hidden.
- Verification: focused command/model tests and native full-resolution checks,
  including prefab creation/editing, local lamps and run/test actions.

Preserve stable IDs, Undo, small source patches, runtime isolation and existing
user layouts. New controls use component metadata and shared services rather
than a parallel editor-specific engine model.

## Implementation and native checks (2026-10-10)

The Inspector uses shared component metadata; editable object collections opt in
through that metadata. Material overrides retain the existing exact-field prefab
replacement form, including literal nulls and removed keys. No new prefab format
was introduced. Creation destination previews and actual creation share one path
resolver. Assets → Create is the primary workflow; File → Create calls the same
compact, type-named dialog.

The three 3D rotation rings follow local/world axes, retain snapping and cancel,
and use ray/plane angular dragging with an edge-on screen-tangent fallback.
Canvas window padding is zero. Display scaling covers controls as well as fonts;
a settings record rescales the saved dock graph without resetting its topology.

Native X11/Vulkan checks used the full 5120×2806 editor client area. Created
`lighting_lab.scene.json`, a floor and a point light through the editor. Added a
sphere through Add Component, then created `desk_lamp.prefab.json` through
Assets → Create. Opened its prefab stage, added/edited/removed a material
override, restored it with Undo and saved. The scene reflected the source edit.
Duplicated the lamp instance and changed its X position to 6; both light pools
updated live. Dragging the green rotation ring changed Y to -1.047 radians;
Undo restored zero without moving the Inspector rows.

Local captures (qualification artifacts, not shipped assets):

- `/tmp/editor-rings-clean.png`, `/tmp/editor-ring-drag.png`,
  `/tmp/editor-ring-undo.png`
- `/tmp/editor-material-undo.png`, `/tmp/editor-two-lamps.png`
- `/tmp/editor-e2e-release-result.png`, `/tmp/editor-debug-tail-final.png`

Run & Test launches the actual selected CLI executable through SDL process APIs.
Release colony E2E reported `passed=1 failed=0`. Debug exposed two older runtime
issues: script properties erased integer types, and zero-cylinder capsules
asserted in Jolt. Properties now use the shared JSON/Lua bridge, integer contracts
normalize validated values, and degenerate capsules use their exact sphere shape.
Debug physics regression and a visible three-frame colony smoke passed afterward.
The full-resolution Debug E2E exceeded its 300-second budget; the panel cancelled
it and reported timeout. This is not a passing Debug E2E result. Windows process
and display behavior have not been qualified on Windows hardware.

The former four-point/four-spot cap is replaced by per-view dynamic light data.
Only the primary directional light casts shadows; local-light shadows remain
unimplemented. The texture owner allocates mutable RGBA32F storage and uploads
changed data. Tests distinguish immutable initial-data textures from mutable
textures. Failed primitive render frames now cancel unfinished batches so the
next valid edit can render again.

## Performance evidence

A fixed 1280×720 Vulkan probe, discarding its first 60 frames, measured:

| Probe | Frame median / p95 ms | Render preparation median / p95 ms | GPU median / p95 ms |
| --- | --- | --- | --- |
| Previous arrays, 4 lights | 5.837 / 8.210 | 0.040 / 0.056 | 0.084 / 0.097 |
| Dynamic storage, 4 lights | 6.746 / 7.136 | 0.037 / 0.062 | 0.092 / 0.094 |
| Dynamic storage, 32 lights | 6.744 / 7.190 | 0.055 / 0.096 | 0.442 / 0.854 |

These are small static-scene samples, not a general performance guarantee.
Overlapping lights increase fragment work. Raw CSVs are `/tmp/lights-before-4.csv`
and the corresponding after-light probe files. Native moving-lamp checks verify
updates separately from the static timings.

## Validation boundaries

The broad 81-test editor/render selection found obsolete HUD override/parent
expectations, the removed precision-widget expectation, and a settings test using
an older function signature; those tests were updated to the current contracts.
The new redundant object-merge implementation was removed after the existing
prefab regression caught it. Corrected authoring/material/physics/Lua tests pass.

Two broader checks remain unresolved: `demi-editor-scene-view-state-tests` spent
more than five minutes at full CPU while loading example content and was manually
terminated; `demi-bgfx-profile-callback-tests` reports “Unexpected callback scope
population”. Neither is claimed green; their causes are not established here.
The Android packaging check in the broader selection passed (150 seconds).

Public documentation was deployed to
`/srv/demi-store/releases/20261010-183300-editor-usability` after Composer tests,
Twig lint and production cache warmup. Atomic activation and `/health` succeeded.

Final focused run: 79/79 tests passed in 14.55 seconds, selecting editor, bgfx,
render-phase6, managed-process, script-property, Lua-scripting and physics3d
checks, excluding the two unresolved checks above and the already-passed Android
packaging test. Colony validation checked 97 files with no diagnostics.
The rebuilt editor was reopened at full resolution; final captures are
`/tmp/editor-final-rotation.png` and `/tmp/editor-final-create-dialog.png`.

## Follow-up interaction and density fixes

Right-button navigation remained captured, but the X11 input trace showed
absolute position changes with zero relative deltas. EditorPointerMotion now
uses absolute differences only when relative deltas are absent, ignores
capture-entry warps, and continues preferring raw deltas at physical edges.
The same native two-part held-button motion probe stopped changing the camera
before the fix and kept rotating afterward. Input tests cover absolute-only
motion, stationary frames, raw-delta precedence and capture transitions.

Prefab row backgrounds now use text-row height rather than padded button height.
Primitive/camera/light creation lives in the existing entity-preset popup.
Toolbar menu buttons use Local's unchanged 30-unit height. Assets and File Create
menus group Scene, Prefab and Terrain; creation labels omit trailing ellipses.
Vector axis labels sit beside their inputs, falling back to stacked pairs only
when the Inspector is too narrow.

The Release editor rebuilt successfully; imgui-input, shell-docking and
drag-authoring tests all passed. Full-resolution native captures:
`/tmp/right-capture-probe-0.png`, `/tmp/right-capture-probe-1.png`,
`/tmp/editor-followup-create.png`, `/tmp/editor-followup-presets-open.png`.
