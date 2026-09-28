# Terrain generation and authoring

Status: first finite-heightfield delivery implemented; qualification recorded in
[docs/terrain-qualification.md](docs/terrain-qualification.md). The expanded
generation, material and water work below is planned, not implemented. This
revision changes the plan only.

## Target experience and visual quality

Choose a seed, landscape preset, biome rules and an asset palette, then press
Generate to create a usable landscape automatically. Painting regions is an
optional way to constrain or override the result, not a prerequisite for every
biome. The result remains editable and regenerable.

Use the two user-provided landscape images as visual references:

- Close-range rocky river valley: detailed rock and ground surfaces, shoreline
  vegetation, convincing water, atmospheric depth and coherent lighting.
- Wide mountain landscape: recognizable ridges and valleys, forests distributed
  according to terrain conditions, shoreline transitions and distant detail.

Matching that direction requires landforms, erosion, materials, meshes,
vegetation, water and rendering to work together. Increasing noise detail alone
is not the target. Heightfields remain the base; placed rock/cliff meshes can
provide local shapes that heightfields cannot represent. Volumetric caves and
terrain excavation remain a separate representation decision.

Do not claim screenshot-level quality until representative scenes demonstrate
it with licensed assets and recorded performance on stated hardware. Exact
appearance depends on the selected art palette and renderer capabilities.

## Authoring contract

Create a terrain, configure its dimensions, resolution, seed and biomes, then
press Generate. Inspect the generated surface directly in the viewport. Paint
biome regions or sculpt adjustments and regenerate without losing manual work.
Generate/Regenerate must be explicit, cancellable and undoable. Preserve the old
terrain until a replacement has completed successfully.

Ordinary brush work must become incremental. Full generation is for changes
that genuinely invalidate the base recipe, not the default response to a local
stroke. The current whole-heightfield rebuild after a stroke is an implemented
limitation to replace in milestone 1 below.

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

## Milestone 1: incremental editing and generation dependencies

This is the next implementation priority, before adding expensive generation
stages to the current full-rebuild brush path.

- [ ] Retain the generated base and editable samples. Track each stroke's dirty
  area, affected layers and dependent chunks. Update local samples, border
  normals, mesh buffers and collision without rebuilding unrelated scene data.
- [ ] Account for smoothing neighbourhoods and downstream edit dependencies.
  Cache intermediate layer results/checkpoints where needed; local edits must
  match the result of a full reference evaluation.
- [ ] Store local before/after changes for Undo/Redo, coalesce drag updates, and
  publish bounded updates while brushing. Document when collision catches up
  with the visual preview; Play must not use stale terrain collision.
- [ ] Add named, toggleable layers for generation, biome overrides, sculpting,
  protection and exclusions. Keep flatten targets and raised offsets distinct.
- [ ] Model dependency invalidation explicitly. Editing tint must not rebuild
  heights or colliders; editing vegetation density must not rerun erosion.
  Global seed changes may invalidate the whole base.
- [ ] Hydrology and erosion can have nonlocal effects. Track their dependency
  region or offer an explicit wider rebuild; never hide incorrect seams behind
  a promise that every operation is local.

Acceptance: a small brush stroke changes only the required samples/chunks and
dependent data; untouched render/collider resources retain identity. Compare
stroke latency, allocations and publication cost against the current baseline.

## Milestone 2: seeds, landscape presets, biome rules and asset palettes

- [ ] Add reusable versioned landscape presets with readable controls and
  previews. Initial targets: mountain range, river valley, rolling hills and
  coastal/island terrain. Presets configure the shared generator, not separate
  hardcoded example algorithms.
- [ ] Define a world seed with stable sub-seeds for landforms, erosion,
  hydrology, biome placement and scatter layers. Chunk loading order and worker
  scheduling must not reshuffle results. Record generator/preset versions and
  input asset hashes with caches and generated-world metadata.
- [ ] Add reusable biome rules using elevation, slope, climate/moisture,
  distance to water, drainage and substrate masks. Define priority, blending
  and conflict resolution; keep biome appearance separable from landform shape.
- [ ] Support fully automatic biome assignment, painted overrides, region
  constraints and protected areas through the same evaluation path. Preview
  biome, slope, moisture and other rule masks in the editor.
- [ ] Add reusable asset palettes that map semantic roles to existing material,
  mesh and prefab assets: exposed rock, soil, sand, snow, wet ground, trees,
  bushes, grass, reeds, cliff pieces and debris. Include variation weights,
  physical scale, spacing, collision policy and LOD metadata.
- [ ] Resolve palette/package dependencies through normal asset discovery,
  cooking and packaging. Validate missing roles, maps, licenses and incompatible
  assets before generation. Do not download or redistribute third-party art
  silently; use developer-selected assets or explicitly installed licensed packs.

Acceptance: seed + preset + rules + palette generates an initial landscape
without manual biome painting. The editor can explain which rules assigned a
region or selected an asset, and developers can override that choice.

## Milestone 3: landforms, erosion and drainage

- [ ] Build coherent macro features: connected ridges, mountain masses, valleys,
  basins, coastlines and plateaus. Layer smaller variation over that structure;
  expose feature size, relief and ridge/valley controls rather than only octaves.
- [ ] Evaluate maintained libraries/tools for erosion and terrain processing
  before writing custom implementations. Compare licensing, portability,
  cancellation, determinism and integration costs with representative probes.
- [ ] Add hydraulic and thermal erosion stages with sediment transport/deposition
  where supported by the selected approach. Retain useful outputs such as flow,
  sediment, exposed-rock and wetness masks for later stages.
- [ ] Compute drainage/basin information for water generation. Define sea level,
  outlets, closed basins and river connectivity; do not generate rivers by
  placing arbitrary splines that flow uphill or end without an outlet policy.
- [ ] Resolve the terrain/water dependency order: initial landform, drainage,
  optional channel/basin carving, final surface/rule masks. Bound any iterative
  refinement explicitly and make results independent of chunk processing order.
- [ ] Support preview and final-quality generation settings with visible status.
  Expensive erosion is a generation/bake operation unless explicitly requested;
  local sculpting must not automatically restart a global erosion simulation.

Acceptance: preset-generated mountains and river valleys have coherent shapes
at wide and close views; watershed/chunk boundaries have no processing seams.

## Milestone 4: automatic high-quality surface materials

- [ ] Assign materials automatically from palette roles and generated masks:
  rock on exposed/steep surfaces, sediment in deposition areas, appropriate
  vegetation ground layers, snow where the biome permits, and wet shoreline
  materials near water. All assignments remain editable.
- [ ] Import and validate PBR material sets, including base colour, normal,
  roughness, optional metalness, occlusion and height maps. Preserve correct
  colour spaces, normal-map conventions, units and texture scale.
- [ ] Replace discrete triangle tints with smooth, height-aware material-layer
  blending. Support suitable steep-slope mapping, macro variation to reduce
  visible repetition, and detail normals. Preserve authored overrides.
- [ ] Choose distance/platform quality settings for texture residency and
  surface detail. Parallax/height shading is not actual geometric displacement;
  distinguish them and use geometry where silhouettes or collision require it.
- [ ] Provide licensed starter palettes with coherent material scale and
  optional high-detail rock/cliff meshes. A preset must not pretend to synthesize
  high-quality source art from a colour or noise setting alone.

Acceptance: generated terrain assigns the selected high-quality materials
without painting every texture, avoids stretched cliffs and obvious layer seams,
and remains readable under multiple lighting conditions.

## Milestone 5: native water integrated with terrain

Design water-body data and hydrology interfaces alongside milestones 2–3;
complete rendering and gameplay integration against the material pipeline.

- [ ] Add distinct river, lake and ocean authoring with stable IDs, editable
  spline/boundary shapes, water level, depth/bathymetry and flow parameters.
  Automatically propose water bodies from drainage/basin data, with manual
  additions, exclusions and overrides.
- [ ] Make river-channel, lake-basin and shoreline carving non-destructive
  terrain layers. Changing water level or a river path invalidates the required
  terrain, wetness, material and scatter data while preserving unrelated edits.
- [ ] Render water with depth-dependent colour/absorption, Fresnel response,
  reflections, refraction where supported, animated surface normals/waves and
  shoreline/flow foam. Correctly handle river-to-lake/ocean transitions and
  transparent ordering. Identify renderer prerequisites rather than faking
  equivalent behaviour with an opaque tinted plane.
- [ ] Add underwater appearance and camera transitions, plus shared queries for
  surface height, depth, containment and flow. Expose gameplay APIs/components
  for water interaction; provide opt-in buoyancy/drag and swimming integration.
- [ ] Keep render detail separate from gameplay water queries. A distant or
  culled water surface must not disappear from gameplay. Synchronize query and
  visible-wave conventions where objects interact with the surface.
- [ ] Keep full fluid dynamics/flood simulation outside the initial water scope.
  Lakes, rivers and oceans must work without requiring that simulation. Design
  later extensions without coupling the core contract to one wave technique.

Acceptance: a generated river feeds a lake or ocean, shorelines track water
level, wet materials and vegetation exclusions update, and floating/swimming
probes agree with water queries. Verify above-water and underwater views.

## Milestone 6: procedural vegetation and scene detail

- [ ] Scatter palette assets using biome, slope, moisture, water-distance and
  exclusion rules. Provide separate grass/tree/rock density controls, clusters,
  spacing, scale/rotation variation and an overall art-density setting.
- [ ] Give generated placements stable identities from seed, region and layer.
  Editing one region must not reposition unrelated forests or rock formations.
- [ ] Respect roads, water, buildings, protected areas and explicit painted
  exclusions. Allow chosen generated instances to become authored objects that
  are no longer replaced by procedural regeneration.
- [ ] Use instancing, culling, appropriate LOD and selective collision. Include
  vegetation shading/wind requirements in rendering work; do not create one
  authored entity or full physics body per grass blade.

Acceptance: automatic forests, shoreline reeds and rock formations follow the
landscape rules, remain stable across regeneration, and have measured density
and rendering costs.

## Milestone 7: rendering, cooking and large-world performance

- [ ] Audit required renderer support for PBR layers, directional-shadow quality,
  indirect/environment lighting, reflections, atmospheric depth, anti-aliasing
  and water passes. Implement missing shared renderer capabilities instead of
  terrain-example-only shaders or baked screenshots.
- [ ] Add seam-safe terrain LOD, camera-driven streaming and coordinated water,
  material and scatter residency. Preserve collisions near gameplay actors.
- [ ] Add cooked heightfields/meshes, derived masks and placement caches through
  the same generator used by runtime/editor. Cache keys include all relevant
  stage inputs; store outputs in build/cache locations, not developer asset
  directories. Ship only required audited outputs and dependencies.
- [ ] Support bounded work scheduling, progress, cancellation and transactional
  replacement. Keep the old visible/collision state until replacement is safe.
  Expose configurable memory/work budgets instead of arbitrary content caps.
- [ ] Qualify desktop at 1080p and 1440p, including dedicated and integrated GPUs,
  plus physical Android devices with explicit quality tiers. Measure generation,
  local edits, frame-time percentiles, GPU cost and memory separately. Establish
  targets from representative scenes before promising map sizes or FPS.

## Milestone 8: reference scenes and acceptance

- [ ] Build two reproducible generated scenes matching the reference themes:
  a close-up rocky river valley and a wide forested mountain/coastal landscape.
  Record seed, preset, rules, palette versions, quality tier and any manual edits.
- [ ] Capture fixed viewpoints in motion as well as stills: ground-level material
  detail, mountain silhouettes, forest transitions, shorelines and underwater
  views. Inspect tiling, LOD popping, temporal shimmer and water transitions.
- [ ] Demonstrate the entire editor workflow: generate without painted regions,
  inspect rule masks, change palette/preset, sculpt locally, protect a site, edit
  a river, regenerate, Undo/Redo, save, reopen and package.
- [ ] Compare visual results against the user's references and report remaining
  gaps honestly. Document asset requirements and measured performance; do not
  present a hand-polished one-off scene as proof of general automatic generation.

## Validation criteria

- Fixed seed and recipe produce reproducible results; global coordinates keep
  chunk boundaries continuous, including lighting normals and collisions.
- Define reproducibility across generator versions and CPU/GPU backends. Do not
  promise bit-identical output across platforms without testing it. Detect stale
  caches and incompatible saved terrain explicitly; never silently discard edits.
- Regeneration preserves manual adjustments and unrelated scene content.
- Failed/cancelled work cannot replace the last good terrain or overwrite newer
  source edits. Undo/Redo restores the recipe and visible surface together.
- Brush picking uses the actual surface and respects the terrain transform.
- Invalid inputs produce actionable errors; allocation/index overflow is checked.
- Generated geometry is absent from authored scenes/prefabs and asset folders.
- Measure generation latency, publication cost, mesh/collider memory and frame
  time before making scalability claims. Finite chunking alone is not streaming.
- Test rivers/basins crossing chunk boundaries, overlapping biome rules, material
  and asset failures, protected settlements, water-level changes, cancelled work,
  asynchronous stale results and save/reload after local edits.
- Update public guidance in tools/package-store/templates/docs/content/ and
  register the terrain page in Docs.php; put qualification evidence in docs/.

## Decisions

- Heightfield first; shared biome authoring can later feed another representation.
- FastNoiseLite is the initial noise dependency, pinned to a reproducible version.
- No automatic relocation/deletion of buildings when terrain changes. Placement
  adjustment and ground snapping remain explicit developer actions.
- No silent edit discard when changing resolution or dimensions.
- Automatic material/asset selection uses declared palettes and licensed input
  assets. It does not imply automatic downloading, AI-generated art or access to
  the source assets in the reference images.
- The current baseline is not the visual-quality milestone. Completion of the
  expanded plan requires both the authoring workflow and reference-scene gates.
