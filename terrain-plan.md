# Terrain generation and authoring

Status: first finite-heightfield delivery implemented; qualification recorded in
docs/terrain-qualification.md. Follow-up work remains below. Caves and excavation
require a separate volumetric representation.

## Authoring contract

Create a terrain, configure its dimensions, resolution, seed and biomes, then
press Generate. Inspect the generated surface directly in the viewport. Paint
biome regions or sculpt adjustments and regenerate without losing manual work.
Generate/Regenerate must be explicit, cancellable and undoable. Preserve the old
terrain until a replacement has completed successfully.

Store authored recipes, region strokes and manual layers, not generated meshes
as developer-facing assets. Terrain generation is shared by editor and runtime;
the editor does not own a second generator. Existing scene objects are separate
from terrain generation and must not be removed by regeneration.

Manual edits have explicit semantics: raise/lower adds an offset; flatten keeps
a target height; smooth filters neighbouring heights; protection retains sampled
heights across regeneration. Changes to dimensions/resolution require an explicit
decision when existing edits cannot be preserved safely.

## First delivery: usable finite heightfield terrain

- [x] Native Terrain3D recipe/component with shared validation and defaults.
- [x] Deterministic noise from a pinned library, biome region blending and
  editable terrain height controls; no arbitrary map-size/content caps.
- [x] Chunk meshes with consistent border positions/normals, viewport/runtime
  parity and static collision matching the generated surface.
- [x] Non-destructive raise/lower, flatten, smooth and protected-area data.
- [x] Editor Generate/Regenerate controls with draft settings, background work,
  progress, cancellation and transactional publication.
- [x] Biome and sculpt brushes with radius/strength/falloff, surface picking,
  visible brush feedback, Undo/Redo and normal source Save/reload.
- [x] Example project, maintained website documentation, focused core/editor
  tests and visible Linux verification.

## Follow-up delivery: assets, surfaces and large worlds

These remain explicit follow-up work, not claims made by the initial terrain
tool. Keep the recipe/generator boundary extensible for these capabilities.

- [ ] Reusable biome assets, material layers, slope/altitude rules and smooth
  material blending. Keep terrain shape independently configurable from biome
  appearance where desired.
- [ ] Vegetation/prop distributions: separate density controls, spacing,
  clustering and exclusions; deterministic placement that does not reshuffle
  untouched regions. Use instancing rather than an authored entity per blade.
- [ ] Dirty-region generation including boundary neighbours, chunk LOD and
  camera-driven streaming with explicit CPU/GPU/memory budgets.
- [ ] Cooked terrain caches and runtime generation through the same pipeline;
  cache invalidation by recipe/generator version, never authored generated paths.
- [ ] Named/toggleable edit layers, road/river tools and placement constraints.
- [ ] Physical Android performance and editor large-map qualification.

## Validation criteria

- Fixed seed and recipe produce reproducible results; global coordinates keep
  chunk boundaries continuous, including lighting normals and collisions.
- Regeneration preserves manual adjustments and unrelated scene content.
- Failed/cancelled work cannot replace the last good terrain or overwrite newer
  source edits. Undo/Redo restores the recipe and visible surface together.
- Brush picking uses the actual surface and respects the terrain transform.
- Invalid inputs produce actionable errors; allocation/index overflow is checked.
- Generated geometry is absent from authored scenes/prefabs and asset folders.
- Measure generation latency, publication cost, mesh/collider memory and frame
  time before making scalability claims. Finite chunking alone is not streaming.
- Update public guidance in tools/package-store/templates/docs/content/ and
  register the terrain page in Docs.php; put qualification evidence in docs/.

## Decisions

- Heightfield first; shared biome authoring can later feed another representation.
- FastNoiseLite is the initial noise dependency, pinned to a reproducible version.
- No automatic relocation/deletion of buildings when terrain changes. Placement
  adjustment and ground snapping remain explicit developer actions.
- No silent edit discard when changing resolution or dimensions.
