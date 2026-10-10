# Typed asset authoring qualification

## Scope

The editor can create terrain surface materials, material sets and asset palettes
from the Assets menu or File/New document. Sources are ordinary versioned data
documents with imported DataAsset manifests. Material sets start with an existing
material; palettes start with a model or prefab reference. Creation does not
invent project-specific asset IDs.

Typed controls edit maps, surface parameters, named material roles and placement
rules. Reference picking and color editing are shared ImGui controls; source
commands and validation remain UI-free. Unknown DataAsset content types retain
the generic document editor. Surface definitions are not a shading preview.

## Shared validation and persistence

`DataAssetContent` adapts existing native terrain parsers for import/reimport and
editor validation. The CLI accepts `asset import --content-type` on DataAssets
through the same importer. Generic data does not guess references from strings.
Asset references become manifest dependencies; prefab references remain native
palette references, not asset IDs in the asset dependency graph.

Edits validate against prospective dependencies without mutating the manifest.
Save patches the source and then reimports. If reimport fails after saving the
source, the document remains import-pending: Save can retry, recovery retains
the pending state, and switching documents cannot silently discard it. Import
validation failure leaves the previous manifest metadata intact.

## Verification

Focused checks cover native import validation, source creation, typed ImGui
controls, changed-reference dependencies, Undo/Redo, pending reimport/recovery,
specialized documents, existing game/drag authoring, graph settings and docking.
Release editor/CLI and the affected test targets build successfully. The final
focused gate passes all eight checks (0.68 seconds):

```sh
ctest --test-dir build/linux-release --output-on-failure \
  -R '^demi-(data-asset|editor-(data-asset-creation|data-asset-controls|specialized-document|game-authoring|drag-authoring|terrain-graph-settings|shell-docking))-tests$'
./build/linux-release/demi validate examples/terrain_3d
./build/linux-release/demi validate examples/terrain_graph_3d
```

The examples validate without diagnostics (25 and 4 files respectively).
An isolated temporary project also passes CLI import with
`--type DataAsset --content-type terrain_material` and native Vulkan editor
startup, captured from its PID/title-verified window at 5120×2806. This is a
scoped gate, not a full repository or platform qualification. `git diff --check`
is clean.

An attempted capture using the larger `terrain_3d` project did not produce a
verified native window before the capture timeout. It is not visual qualification
of the typed editor. The controls tests use actual ImGui frames but do not qualify
interactive desktop drag/drop or Android behavior.

## Remaining work

Terrain material-layer rendering, smooth blending, texture residency, visible
water and landscape art quality remain open in `terrain-plan.md`. Authoring
these definitions does not mean terrain meshes render their PBR properties.

## Package refresh and drop context (2026-10-10)

A live package upgrade reproduced an editor abort in packageContentHash through
filesystem::relative while an installed directory was being replaced. Hash labels
now use the already-constructed relative package paths, and the shared package
loader converts transient filesystem failures into diagnostics. A 1,000-scan
concurrent directory-swap test passes, as does a native reinstall with the editor
open. This does not weaken manifest checks or publish partial package files.

Dropping a prefab into Viewport while HUD was the active document also left the
Inspector on HUD. The viewport now reports activation/delivered drops to the
shell, which follows the receiving document and its selection. A real ImGui drag
fixture verifies Scene focus, selection, HUD isolation and Undo. Native depot
placement and subsequent selection were checked at full editor resolution.

## Native entity-prefab editing and dependent preview refresh (2026-10-10)

The colony depot and engineer were opened from Assets in the native Prefab stage
at 5120 x 2806. The depot rack was renamed, saved, undone and saved, then redone
and saved. A disk assertion confirmed Undo restored the original absent name.
The engineer cargo child was enabled to inspect the carried crate and undone;
that temporary enable was not saved. Captures:

- `/tmp/prefab-rack-saved.png`
- `/tmp/prefab-engineer-cargo-enabled.png`
- `/tmp/prefab-engineer-cargo-undo.png`

This exposed a missing dependency refresh: prefab source saves left existing
scene instances stale. Save All and Save Active now recompose authoring previews
from their current documents, preserving overrides, drafts, history and surviving
selection. The shared scene loader remains responsible for composition; Play
continues to own a separate world. Failed recomposition retains the last usable
preview. Document-session tests cover saved source changes, scene draft and
override preservation, Undo/Redo followed by Save, and malformed dependencies.

After rebuilding, the depot rack was changed to `Supply rack` and height 1.6 in
the Prefab Inspector. Saving immediately updated the open scene instance's name
and MeshRenderer size without reloading the project; its expanded ID stayed
`depot/rack`. `/tmp/prefab-fixed-instance-inspector.png` records the result.
The saved source diff contains only the rack name and height. These clearer
labels and proportions remain in the example.

Four focused CTests passed: editor document sessions, prefab authoring, prefab
components and shell docking. The nested-parent Inspector presentation issue is
recorded in `tofix.md`; this qualification does not claim it is fixed.
