# Terrain palette separation proposal

Status: accepted by the user and implemented as the format-2 authoring migration.
Prepared against commit `3e2e82bb`. Automatic mask-based surface shading remains
a separate unfinished integration gate; ordinary explicit biome materials render.

## Problem confirmed in source

`TerrainPalette` currently has one fixed role map containing both surface names
and object names. `scatterTerrain` treats every entry as a placement candidate.
The meadow example therefore supplies a cube prefab even for soil, sand, snow
and wet ground. The palette loader requires an asset reference even when a
prefab already owns its geometry and materials. These are authoring-contract
problems; reducing density or imposing a smaller placement limit would conceal
them.

Several milestone-2 paragraphs in the plan predate implemented scatter
publication. In particular, placements now become scene entities. The migration
should reconcile those stale status paragraphs against tests, not mark entire
material or vegetation milestones complete.

## Recommended public format

Keep one selectable palette asset, referencing the existing reusable surface
material-set asset and containing independently named object placement rules:

```json
{
  "format_version": 2,
  "name": "Meadow shoreline",
  "material_set": "asset://terrain/material_sets/meadow",
  "placements": {
    "pine_trees": {
      "prefab": "prefab://vegetation/pine",
      "weight": 0.15,
      "scale": [0.9, 1.3],
      "spacing": 12,
      "biomes": ["meadow"],
      "collision": "static"
    },
    "shore_reeds": {
      "model": "asset://vegetation/reeds",
      "weight": 0.7,
      "spacing": 0.75,
      "biomes": ["shore"],
      "collision": "none"
    }
  }
}
```

The references above illustrate the contract; they are not claims that these
art assets exist in the repository or public store.

- `material_set` is optional and uses the existing `terrain_material_set`
  contract and its surface roles. This avoids a second surface-binding schema.
  Material bindings never emit scene entities.
- `placements` is optional; a palette can supply surfaces, objects, or both,
  but must supply at least one. Keys are durable rule IDs chosen by the author,
  not the old eleven-role enum. Renaming a rule changes its generated identities.
- Each placement requires exactly one of `model` or `prefab`. Models must resolve
  to Model3D; prefab geometry/materials remain owned by the prefab. A separate
  dummy material reference is no longer required for a prefab placement.
- Retain existing weight, scale, spacing, biome filter, collision and LOD
  metadata semantics for this migration. Do not introduce new density units,
  silently lower density, or claim that LOD metadata implements vegetation LOD.
- Random selection uses the rule ID, terrain seed and cell. Scene reconciliation
  retains owner/palette/rule/cell identity for surviving placements. Use a
  specified stable hash, never implementation-defined
  `std::hash`, and verify independence from JSON object ordering.
- Explicit biome material assignments remain authored overrides. Connecting
  the material set to generated surface masks is a shared surface-stage change;
  parsing the new palette alone does not complete automatic material rendering.
  Typed PBR maps and smooth layer blending remain separate unfinished gates.

## Migration and ownership

The palette parser/validator owns version checks and reference-type errors.
Asset discovery owns dependencies and prefab traversal. Scatter owns placement
rules and identity; surface material resolution owns surface bindings. The
editor presents these same contracts in separate Surface Materials and Object
Placements sections. It does not expand a second runtime representation.

Migrate repository examples, schemas, creation templates, editor controls,
CLI inspection/explanation, cooked placement data and matching tests together.
Old authored palettes receive an explicit version/migration diagnostic rather
than reinterpretation. Invalidate derived terrain payloads through their normal
version and dependency keys. Do not silently discard saves or manual edits.

The meadow example's ground roles become surface bindings. Actual vegetation,
cliff and debris entries remain object rules with their original authored
weights and spacing. Prototype geometry stays clearly identified as prototype
art; this change does not imply reference-image quality.

## Acceptance gates

1. A surface-only palette generates zero scatter entities; a mixed palette
   scatters only its named placement rules.
2. Two named rules can reference the same model/prefab with distinct spacing,
   biome filters and stable identities. Prefab-only rules need no dummy asset.
3. Invalid versions, references and model/prefab combinations fail clearly.
   Import, inspect, validation and cook agree on the contract and dependencies.
4. Reordering rules preserves output; local sculpting retains placement IDs;
   Undo/Redo, exclusions and shared terrain instances remain correct.
5. Source and cooked loading agree. Surface appearance edits do not rerun
   landform generation or replace unaffected collision resources.
6. Exercise palette creation/editing and regeneration in the native editor,
   validate migrated examples, and record renderable counts and timings without
   reducing authored object density to manufacture a passing result.

Implementation evidence: [qualification](terrain-palette-qualification.md).
