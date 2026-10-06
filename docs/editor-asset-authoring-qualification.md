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
