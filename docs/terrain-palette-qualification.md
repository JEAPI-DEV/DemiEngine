# Terrain palette format-2 qualification

## Delivered contract

The agreed palette separation is implemented in shared native parsing, typed
DataAsset version validation, import/reimport, editor creation and field controls,
scatter execution, instance reconciliation, cache identity and cooked metadata.

`material_set` references the existing surface-role contract. `placements` maps
stable author-chosen rule IDs to exactly one Model3D `model` or entity `prefab`.
Rules can share geometry with different metadata; prefab-only entries no longer
need a dummy material asset. Surface-only palettes have no object placements.
The editor supports creation from a material set alone, named-rule editing,
model/prefab switching, placement removal, normal history and dependency-aware
Save. Native typed dependencies are checked even without a JSON schema.

Version-1 palettes fail explicitly; repository content was migrated rather than
kept behind a legacy alias. Terrain generator and asset-envelope versions are
now 5, invalidating derived data. Source document IDs, recipe format and material
set format remain unchanged. The new material-set manifest retains portable
source metadata; its optional importer preview-output path is omitted like its
neighboring source assets, so a fresh checkout needs no committed generated data.

Random streams use FNV-1a of rule IDs instead of enum ordinals. Scene instance
identity retains the owner/palette/rule/cell convention, so local sculpting moves
existing instances. Rule renames change identities. Reordering rules does not.
Changing the world seed changes selection/jitter, with reconciliation retaining
surviving cell/rule identities. This preserves the existing ownership behavior.

Surface content hashes are excluded from scatter-generation fingerprints.
Placement settings remain in those fingerprints. A surface-set reference is
retained in cooked metadata and normal asset dependency closure.

## Rendering boundary

This delivers palette separation and authoring, not automatic surface shading.
Explicit ordinary biome materials still render. Applying material-set roles to
surface masks, typed PBR maps, smooth blending and vegetation LOD/instancing
remain integration gates in `terrain-plan.md`. The editor already labels its
terrain PBR shading limitation. Surface-only palettes do not spawn geometry as
a substitute for material rendering.

## Executed checks

- Focused native tests cover surface-only palettes, reusable geometry under
  independent named rules, invalid formats/references/ranges, import dependencies,
  source/cooked metadata, cache reuse/invalidation, publication, local edits and
  editor creation/controls.
- `demi validate examples/terrain_3d`: 26 files, no diagnostics.
- `demi validate examples/terrain_graph_3d`: 4 files, no diagnostics.
- Full example cooked to `/tmp/terrain-palette-cooked`; cooked validation checked
  25 files without diagnostics. A three-frame headless cooked run exited normally
  and reported 324,330 renderables.
- Payload inspection counted 323,984 object placements: grass 180,061; reeds
  129,940; bushes 4,727; debris 8,787; cliff pieces 361; trees 108. No ground
  material entries appear in scatter output. Original object weights/spacing
  are retained. Removing surface entries changes their relative-weight
  normalization, and the new named-rule seeds change selection; the migration
  does not promise identical placement counts or reduced runtime cost.
- Full-density editor startup remained CPU-heavy before opening a window and
  was stopped. No density cap or silent example reduction was used to conceal
  this. High-density editor/vegetation qualification remains unfinished.
- Native palette controls were exercised in a separate temporary project with
  the same authored assets but no terrain placement in its scene. At a full
  5120×2806 client size, opened the meadow palette through Assets, renamed `bush`
  to `bush_probe`, changed spacing from 3 to 4, saved, removed that placement,
  restored it with Undo and saved again. The resulting project validated with
  no diagnostics. This qualifies asset editing, not the full-density viewport.

Local evidence:
`/tmp/palette-editor-controls.png`, `/tmp/palette-editor-saved.png`,
`/tmp/palette-editor-undo-native.png`, `/tmp/palette-editor-native-validation.log`,
`/tmp/terrain-palette-cook.log`, `/tmp/terrain-palette-cooked-validation.log`,
`/tmp/terrain-palette-cooked-smoke.log`.

Final focused terrain migration gate: 14/14 tests passed in 6.16 seconds.
