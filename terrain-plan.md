# Terrain generation and authoring

Status: finite-heightfield editing and the Modules/terrain-graph workspace are
implemented. Seeds, presets, biome rules, scatter and native erosion/water data
have qualification coverage. Blended landscape materials, advanced water and
large-world vegetation/streaming still need their integration gates below.
Evidence is recorded in [docs/terrain-qualification.md](docs/terrain-qualification.md).

## In-scene authoring and graph settings

- [x] Store shared brush parameters with ordered points instead of repeating
  every field per stamp. Compact existing recipe data without changing native
  operation order, strength, layers or protection snapshots. New viewport
  authoring, the graph's compaction action and the CLI use the shared codec.
- [x] Limit full sculpt replay to conservative brush sample bounds, using the
  same coordinate conversion as incremental edits. Qualify exact equality
  against full-grid replay; retain regeneration and editable layers.
- [ ] Add editable raster/tile authoring for extensive sculpted layers where
  point-based history is no longer the useful source representation. Do not
  silently replace procedural replay semantics with baked absolute heights.

- [x] Cursor-anchored wheel zoom, logical node coordinates independent of view
  scale, and collapsible floating recipe settings without a reserved column.
- [x] Expand the graph example into a branched landscape with drainage-fed
  erosion, biome rules, and connected lake/river data. Keep missing terrain PBR
  and advanced-water rendering explicit.

- [x] Put every generation setting in the Terrain Graph workspace: grid, seed,
  presets, landforms, biomes, rules, palette, layers and connected node parameters.
  Keep one authored recipe; do not introduce an Inspector copy of graph settings.
- [x] Selecting a Terrain asset placement in a scene binds its shared source
  document without replacing the scene viewport with an isolated asset stage.
  Sculpt/paint in context, then apply changes to the asset. Preserve placement
  transforms and asset references; update other placements of the same asset.
- [x] Keep brush tools, radius, strength, falloff, target layer and mask preview
  on the Terrain component. Brush strokes remain local, undoable edits.
- [x] Verify asset Undo/Redo, applying/saving, scene history independence,
  selection changes, cancelled edits and shared placements.
- [x] Update public documentation with the workflow and the actual rendering
  gaps: ordinary biome materials reach terrain meshes; typed terrain material
  definitions and smooth blending are still pending. Native transparent water
  publication is implemented; advanced water shading remains pending.

The standalone Terrain asset stage remains useful for inspecting an asset in
isolation, but must not be required for editing a placed terrain. The large
`terrain_3d` probe uses a conventional recipe and prototype scattered geometry;
it does not demonstrate blended landscape materials or reflection/refraction.

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

Ordinary brush work is incremental. Full generation is reserved for changes
that genuinely invalidate the base recipe, not used as the response to a local
stroke. See milestone 1 below for what is implemented.

Store authored recipes, region strokes and manual layers, not generated meshes
as developer-facing assets. Terrain generation is shared by editor and runtime;
the editor does not own a second generator. Existing scene objects are separate
from terrain generation and must not be removed by regeneration.

Manual edits have explicit semantics: raise/lower adds an offset; flatten keeps
a target height; smooth filters neighbouring heights; protection retains sampled
heights across regeneration. Changes to dimensions/resolution require an explicit
decision when existing edits cannot be preserved safely.

## Active delivery: editable terrain assets and cooked runtime loading

The terrain graph, biome settings and manual layers belong to a separate
`*.terrain.json` asset source, registered through an ordinary `*.asset.json`
manifest of type `Terrain`. A scene places it with `Transform3D` and
`Terrain3D.asset`; it does not embed or duplicate the asset's recipe.

- [x] Register Terrain source parsing, dependency discovery, importer/schema
  validation and stable `asset://` references through shared asset services.
- [x] Persist generated heightfields, base checkpoints, normals, biome/mask
  data, water data and scatter placements. Loading a prepared asset must not
  re-run noise, erosion, water carving or scatter selection.
- [x] Generate/Re-generate and brushes edit the terrain asset document, with
  normal Undo/Redo, Save, conflict detection and recovery. Asset files can be
  opened independently of the main scene and dragged into scenes.
- [x] Reuse prepared data across scene instances while retaining independent
  placement transforms; edits update the shared asset, not a scene override.
- [x] Use the ordinary cook graph/cache and package audit to ship generated
  payloads. Do not ship the authoring recipe as a substitute for cooked data.
- [x] Put generated previews in platform cache directories and shipping
  payloads in build output; never fill developer asset folders with meshes.
- [x] Development tools may prepare missing/stale source assets before loading;
  runtime asset loading itself never generates. Missing/corrupt shipped data
  fails explicitly, with no procedural fallback.
- [x] Keep inline `Terrain3D.recipe` as an explicit procedural-generation
  choice, mutually exclusive with `asset`. Empty components are placeholders.
- [x] Qualify cold/warm asset loading, edited-cache invalidation, local brush
  updates after reopening, multi-instance use, corrupt outputs and cooked-only
  Linux/Android project validation. Verify package outputs omit authoring data.

Qualified on Linux with 59 selected regression tests, a cooked-only example,
an audited Linux bundle and a Vulkan editor smoke. Android source/cooked data
validation passes; device lifecycle/rendering qualification remains separate.

Chunked render/collision remains a runtime implementation detail. One logical
terrain asset does not require one giant collider. Loading includes native mesh
and collision preparation; that is distinct from procedural generation.

## Modules and terrain graph workspace

The next authoring surface is a graph stage backed by the native terrain
generator. It is not a second editor-only generator or a diagram layered over
a fixed sequence of recipe fields.

- Inspector remains the first tab; a searchable Modules palette is the second.
  HUD controls and reusable UI prefabs can be dropped onto the existing HUD
  stage. Terrain modules can be dropped onto the graph.
- Graph nodes and links have durable IDs, typed ports and parameter metadata.
  Height sources, elevation/slope masks, blending, offsets, erosion, biome
  rules, water authoring and scatter can form branching pipelines. Native
  validation rejects incompatible links and cycles.
- Generate publishes a validated result through the existing cancellable,
  transactional terrain workflow. Unapplied graph edits have Undo/Redo;
  applied terrain uses the document's normal Undo/Redo and Save.
- Only ancestors of the chosen Terrain Output execute. Intermediate results
  are cached by content and dependencies; moving a node does not regenerate
  terrain. Changed inputs invalidate their dependent branches.
- Manual biome painting, sculpting, protection and exclusions remain overlays
  over the graph output. Local brush work must not replace the graph's shape
  with the recipe's original noise or replay unrelated graph branches.
- Recipes store the graph and manual layers. Generated meshes and cache data
  never become developer-facing source assets.
- Keep rendering qualification separate: a water node computes water/channel
  data; this alone does not deliver reflected/refracted water, blended PBR
  ground materials, vegetation LOD or the reference images' visual quality.

Qualification must cover native graph execution and cache invalidation, draft
editing, HUD drops, generated preview/runtime parity and preservation of manual
work. The existing terrain example is retained; `examples/terrain_graph_3d`
is the focused branching-graph probe.

### Next design checkpoint: palette materials versus placements

The current palette conflates surface-material roles with geometry placements.
The prototype palette consequently produces soil/sand/snow proxy objects as
well as vegetation and props. The full-density terrain example produces roughly
295,000 renderables; removing quadratic queue/prune work improves startup but
does not resolve that authoring model or qualify its visible performance.

Proposed next contract: keep one reusable palette asset, with separate surface
material bindings and named placement rules. Material bindings should not create
entities; placements should explicitly name a model or prefab. Do not fix this
by imposing another content cap or quietly reducing authored density. Agree the
public format before migrating examples and schemas; alpha migrations are
allowed, but the new format must remain usable for arbitrary materials and props.

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

Implemented. Brush strokes replay locally from the retained generated base, and
dependency invalidation is explicit. Measurement is recorded in
[docs/terrain-qualification.md](docs/terrain-qualification.md).

- [x] Retain the generated base and editable samples. Track each stroke's dirty
  area, affected layers and dependent chunks. Update local samples, border
  normals, mesh buffers and collision without rebuilding unrelated scene data.
- [x] Account for smoothing neighbourhoods and downstream edit dependencies.
  Cache intermediate layer results/checkpoints where needed; local edits must
  match the result of a full reference evaluation.
- [x] Store local before/after changes for Undo/Redo, coalesce drag updates, and
  publish bounded updates while brushing. Document when collision catches up
  with the visual preview; Play must not use stale terrain collision.
- [x] Add named, toggleable layers for generation, biome overrides, sculpting,
  protection and exclusions. Keep flatten targets and raised offsets distinct.
- [x] Model dependency invalidation explicitly. Editing tint must not rebuild
  heights or colliders; editing vegetation density must not rerun erosion.
  Global seed changes may invalidate the whole base.
- [x] Hydrology and erosion can have nonlocal effects. Track their dependency
  region or offer an explicit wider rebuild; never hide incorrect seams behind
  a promise that every operation is local.

  The stage itself is milestone 3 work, so this item is closed by making the
  locality decision owned, single-place and enforced rather than by adding
  speculative configuration. `TerrainRecipe::sameGenerationInputs` is the one
  place that decides whether a change replays locally or rebuilds the base, and
  it lives beside the fields it classifies. `demi-terrain-locality-tests`
  perturbs every generation input, every surface-only input and the layout-only
  inputs, and asserts each is classified on the correct side, so a stage added
  without classifying it fails the gate rather than producing a seam.

  When erosion or hydrology lands, its parameters must be added to
  `sameGenerationInputs` with a matching test case. A stage that cannot be
  replayed locally should first be registered here as a global input, so strokes
  fall back to a full rebuild, and only then be narrowed to a tracked dependency
  region once the influence radius is measured.

Acceptance: a small brush stroke changes only the required samples/chunks and
dependent data; untouched render/collider resources retain identity. Compare
stroke latency, allocations and publication cost against the current baseline.
Met: a radius-3 stroke at 256 and 512 cells changes 47 samples, retains 3,688
bytes of local history independent of grid size, skips base evaluation entirely,
and does not rebuild untouched chunk resources. Measured cost and remaining
limits are recorded in
[docs/terrain-qualification.md](docs/terrain-qualification.md).

## Milestone 2: seeds, landscape presets, biome rules and asset palettes

Status, item by item. Two items are complete. Three are substantially delivered
with a named remainder, and one is mostly outstanding. The unchecked boxes below
are deliberate: a box is only ticked when the item's own wording is satisfied.

- Presets: complete, including the editor picker, the command-line verbs and all
  four named targets.
- Seeds: the sub-seeds, the generator version and the input asset fingerprint are
  all in the cache key, so the item's caching half is done. It stays unticked
  only because "generated-world metadata" has no standalone record; provenance
  currently lives on the field itself.
- Biome rules: every condition the current generator can supply is implemented
  with priority, blending and conflict resolution. One thing the item names is
  not: drainage, which needs the flow accumulation of milestone 3, and
  separability of appearance from landform shape. A `TerrainBiome` still carries
  `base_height`, `height_variation`, `feature_size`, `roughness` and `octaves`
  alongside its `color`, so changing how a biome looks still means changing how
  it is shaped. Palettes separated appearance for scattered props, not for the
  biome surface itself.
- Automatic assignment: assignment, painted overrides, protected areas and the
  layer toggle all work through one evaluation path, and biome, elevation and
  slope masks preview in the editor. Two gaps: region constraints are not
  implemented at all, and moisture is deliberately absent from the mask preview
  because it is not stored on the field.
- Asset palettes: definition, validation, biome filtering, generation-time
  scattering and instantiation are implemented. The example still points at
  placeholder material assets because authoring mesh art was out of scope.
- Palette dependencies: manifest declaration is enforced by the existing
  data-asset validator and covered by `demi validate`. Role requirements are
  checked at load. Asset map, licence and compatibility validation is not
  implemented, and the plan's instruction not to redistribute third-party art
  silently is honoured only by documentation.

In progress. Sub-seeds, the generator-versioned cache, biome rules, the preset
document and the palette document are implemented and tested, and the editor and
CLI can author, apply and explain rules and presets. What is missing is anything
that consumes a palette at generation time, and the drainage condition.

- [x] Add reusable versioned landscape presets with readable controls and
  previews. Initial targets: mountain range, river valley, rolling hills and
  coastal/island terrain. Presets configure the shared generator, not separate
  hardcoded example algorithms.

  A preset is a reusable, versioned
  `DataAsset` with `settings.content_type: "terrain_preset"`, referenced by a
  normal `asset://` URI. It is a recipe fragment that an author applies into
  their recipe once, not a second generator and not a runtime override: the
  recipe stays the single source of truth for generation, so incremental
  editing, the locality contract and undo/redo stay trivially correct and a
  preset can never disagree with the terrain it was applied to. Applying
  supplies size, resolution, chunk cells, seed, default biome, biomes, layers
  and rules, and stamps `preset_id`/`preset_version` so the recipe is
  self-describing and regenerates identically forever; both keys are validated
  as a pair, and provenance is compared by `sameGenerationInputs`, so applying a
  different preset is a real recipe change. Strokes are authorial and are never
  templated: applying a preset cannot destroy someone's painting or sculpting,
  and the merged document is validated so strokes are checked against the biomes
  they will actually be generated with. Omitted keys fall back to canonical
  defaults, so a preset that only sets a seed still yields a valid recipe.

  A preset now has both surfaces the item asks for. The terrain inspector gained
  a Landscape preset section that lists the project's presets from the asset
  registry, shows the selected preset's name, label and description, and offers
  Apply and Clear; applying is one undoable recipe edit with the native sample
  patch, so Undo/Redo moves the recipe and the visible surface together. A
  preset that changes size or resolution is not applied blindly: with protection
  edits present the engine's own grid guard refuses it, and otherwise it routes
  through the existing resize decision so strokes are resampled only on the
  author's explicit choice. The CLI gained `demi terrain presets <project>` and
  `demi terrain apply-preset <project> --preset <asset://id>`, and the latter
  writes through the shared source-preserving patcher so a scene edit stays a
  small reviewable diff. `examples/terrain_3d` ships a mountain-range preset with
  five biomes and six elevation and substrate rules.

  All four named targets are authored in `examples/terrain_3d`: mountain range,
  river valley, rolling hills and coastal island. Each was verified by applying
  it and probing the result with `demi terrain explain`, which caught two rules
  that matched no sample at all.

  Outstanding, and the reason this is not ticked: nothing applies a preset.
  Two things the plan names are still not delivered. There is no preview of what
  a preset will produce before applying it; the inspector shows the preset's name,
  label and description, not a rendered result. And there is no runtime override:
  a preset is applied into the recipe, so changing one means regenerating rather
  than re-deciding mid-session.
- [ ] Define a world seed with stable sub-seeds for landforms, erosion,
  hydrology, biome placement and scatter layers. Chunk loading order and worker
  scheduling must not reshuffle results. Record generator/preset versions and
  input asset hashes with caches and generated-world metadata.

  Sub-seeds and the generator version are implemented. `TerrainSeedChannel`
  derives an independent sub-seed per channel from the world seed, hashing the
  channel *name* so that inserting or reordering channels cannot change an
  existing channel's value. The channel set is stable and already reserves
  erosion, hydrology and scatter. Biome noise now samples the Landform
  sub-seed, and moisture samples BiomePlacement, both as single shared fields
  rather than per-biome, so adding a biome does not reshape existing terrain.
  `terrainGeneratorVersion` is folded into the terrain generation cache key, so
  an engine update can never be served a heightfield the current generator
  would not produce; `terrainGenerationCacheKey` exposes the key so that
  contract is testable. The seeded values are pinned by
  `demi-terrain-seed-tests`, so changing the derivation is an explicit
  generator-version bump.

  All five channels are now actually consumed, not merely reserved: landform
  drives the biome noise, biome placement drives moisture, and scatter drives
  instance placement. Erosion and hydrology remain reserved.

  Preset versions are already in that key, because `preset_id` and
  `preset_version` are part of the canonical recipe JSON the key hashes. The
  recipe's `palette` id is in it for the same reason, so changing a palette
  forces a full rebuild instead of a local replay.
  Input asset hashes are now folded in as well, and that closed a real staleness
  bug. The recipe stores a palette by id, and an id does not change when the
  file's contents do, so editing a palette's weights, spacing or biome filters
  left a cached heightfield carrying the previous palette's placements reachable
  under the same key. The cache key now mixes a content fingerprint that the
  caller resolves from the asset registry, the field records the fingerprint it
  was built from, and `demi-terrain-seed-tests` asserts that different content
  produces a different key while identical content still hits.

  No standalone generated-world metadata record exists yet; the field carries its
  own provenance (generator version via the key, palette id and fingerprint) and
  that is the only consumer. The generator is currently single-threaded, so the
  ordering guarantee is structural (each channel is a pure function of the world
  seed) rather than defended against a scheduler that does not exist yet.
- [ ] Add reusable biome rules using elevation, slope, climate/moisture,
  distance to water, drainage and substrate masks. Define priority, blending
  and conflict resolution; keep biome appearance separable from landform shape.

  Rule conditions and resolution are implemented for every input that the
  current generator can supply. `TerrainBiomeRule` bands elevation, slope,
  moisture and distance to water, and can key on derived substrate. Rules
  evaluate against a per-sample `TerrainRuleContext` built from the finished
  base surface, so slope and the multi-source chamfer water-distance transform
  see the whole field. Highest priority wins; an equal priority is broken by
  blend weight, and a full tie keeps the earlier rule, so assignment never
  depends on iteration order. `blend` softens a rule's claim just above its
  elevation band's lower edge, and that strength is the tie-break value; it does
  not cross-fade biome tint, because tint is a discrete biome per cell. The
  distance to water and substrate conditions are derived from the finished
  surface; drainage remains, because it needs the flow accumulation from
  milestone 3. `blend` ramps the
  transition along elevation instead of forming a hard staircase. Drainage
  remains, because it needs the flow accumulation from milestone 3.
- [ ] Support fully automatic biome assignment, painted overrides, region
  constraints and protected areas through the same evaluation path. Preview
  biome, slope, moisture and other rule masks in the editor.

  Automatic assignment and painted overrides are implemented. A recipe with no
  `rules` behaves exactly as before, so the feature is opt-in. Rules run as a
  field pass between generation and sculpting; painted regions are applied
  after them and always win, keeping manual override authoritative. Disabling
  the biome layer removes its rules exactly as it removes its regions. Because
  rules read the whole field they are registered in
  `TerrainRecipe::sameGenerationInputs`, so editing a rule forces a full base
  rebuild rather than a local replay. Each decision records the rule that
  assigned it, and both the editor and the CLI now use that: the editor exposes a
  committed-field rule mask overlay, and `demi terrain explain` reports the
  assignment with every band marked inside or outside against the sample.

  Outstanding: the mask overlay covers biome, elevation and slope but not
  moisture, which is derived in a transient pass and not stored on the field, so
  drawing it would cost a field-wide pass per frame or show a fabricated value.
  Region constraints and protected areas are not yet expressed as rule
  conditions.
- [ ] Add reusable asset palettes that map semantic roles to existing material,
  mesh and prefab assets: exposed rock, soil, sand, snow, wet ground, trees,
  bushes, grass, reeds, cliff pieces and debris. Include variation weights,
  physical scale, spacing, collision policy and LOD metadata.

  The document, loader and validation are implemented; placement is not. A
  palette is a `DataAsset` with `settings.content_type: "terrain_palette"`, so
  it inherits cook, packaging, dependency closure, `demi validate` coverage and
  hot reload from the existing data-asset pipeline rather than needing a new
  document kind. `TerrainPalette` covers all eleven roles with an
  `asset://` reference, an optional `prefab://` prefab, a variation weight, a
  physical scale range, spacing, a collision policy and a LOD level, and
  rejects an unknown role, an inverted scale, a non-positive weight floor, a
  negative spacing, an unknown collision policy, a negative LOD and an
  unresolvable reference. The example palette in
  `examples/terrain_3d` references material assets, because authoring real mesh
  art is outside this milestone; the loader resolves references without checking
  asset type, so meshes can replace them one for one without touching the
  palette, its schema or the loader. Scattering and instancing those roles onto
  the terrain is follow-up work and is why the item stays open.
- [ ] Resolve palette/package dependencies through normal asset discovery,
  cooking and packaging. Validate missing roles, maps, licenses and incompatible
  assets before generation. Do not download or redistribute third-party art
  silently; use developer-selected assets or explicitly installed licensed packs.

  Half of this is done. A palette's `asset://` and `prefab://` references are
  retained by `TerrainPalette`. Typed import/reimport derives asset references
  into manifest dependencies, while prefab references remain in the native
  source-level prefab traversal rather than masquerading as asset IDs.
  Schema-declared references also receive `DATA_DEPENDENCY_UNDECLARED` checks.
  Missing roles, map
  compatibility and licence validation are not implemented, and the example
  palette deliberately points at placeholder material assets. All four example
  presets declare their own dependencies and `demi validate` covers them, so the
  declaration path is exercised by real content rather than only by a fixture.

Acceptance: seed + preset + rules + palette generates an initial landscape
without manual biome painting. The editor can explain which rules assigned a
region or selected an asset, and developers can override that choice.

The acceptance line is met on the generation side and unmet on the asset side.
`demi terrain explain` names the rule that assigned a cell and reports every
band as inside or outside, which answers the "why is my terrain one biome"
question; `demi terrain presets` and `demi terrain apply-preset` make presets
reachable and prove that strokes survive; and `scatterableRoles` reports a
palette that cannot contribute rather than failing generation silently. What is
missing is that a selected asset is still only recorded, not spawned, so no
developer can yet override an asset choice in a running world.

## Milestone 3: landforms, erosion and drainage

In progress. `TerrainPipeline` now runs the landform, surface, sculpt, drainage,
erosion, hydrology and mask stages in a declared order and reports which of them
actually ran, so a caller asks rather than infers. Erosion is refused, and
reported as not run, when a recipe has sculpt strokes, because the pipeline
cannot yet replay them onto an eroded base and losing an author's sculpting
silently is worse than not eroding. The checkboxes below still describe what is
missing, not what is present.

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

- [x] Render ordinary Material asset references assigned to biomes through the
  shared Mesh Renderer path. Store positive finite texture scale in terrain-local
  units, with continuous UVs across chunks. Preserve geometry/collision on
  material changes and only update UV resources for scale changes. Retain both
  values in cooked fields so shipped loading does not need authored recipes.
  Typed terrain material definitions and smooth layers remain unfinished.
  See [binding qualification](docs/terrain-material-binding-qualification.md).

Partly implemented. The PBR document model, its import and its validation are
done and tested as `TerrainMaterialAsset` / `TerrainMaterialSet`, and
`TerrainMaterialLayers` assigns a role to every cell by SCORING the generated
masks rather than by matching names: steep becomes rock, flat and wet becomes
wet ground, submerged becomes underwater, high and flat becomes snow while high
and steep does not, and deposition or flow becomes sediment. Thresholds are
slope degrees, world-unit distances and normalised moisture, so a terrain scaled
ten times produces identical roles and identical scores. Unbound and unused
roles are reported rather than left as a silently flat surface. The layered
blending that replaces discrete triangle groups, and the quality-driven
residency, are not implemented. Note the naming: `MaterialAsset`
already meant the render material in `RenderAsset.h`, so the terrain types are
prefixed to avoid an ODR collision.

- [x] Create and edit surface materials, material sets and scatter palettes
  through the editor without hand-writing JSON. Reuse shared reference/color
  controls and native validation, retain source formatting and Undo/Redo, and
  reimport dependencies when saving. Ordinary renderer materials remain valid
  material-set choices. Failed reimport keeps a retryable, recoverable pending
  state rather than pretending the asset was imported successfully.
  See [asset authoring qualification](docs/editor-asset-authoring-qualification.md).
  This delivers authoring, not terrain PBR shading or visible water.

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

Partly implemented. River, lake and ocean authoring, non-destructive carving and
render-ready water surfaces with per-vertex depth are done and tested, as are
gameplay queries kept deliberately independent of render tessellation. Graph
water outputs now publish native transparent meshes in editor previews and
runtime, including prepared assets. They inherit terrain transforms/visibility,
never acquire solid collision, and reconcile atomically on Generate and history
updates. Basic direct lighting uses the shared transparent mesh renderer.
Graph-authored shallow/deep RGBA, absorption distance and roughness now produce
depth-derived vertex colours through the shared mesh renderer. Shore geometry
clips against terrain triangles; queries use the same interpolation without
depending on rendered meshes. Uncontained bounded lakes warn rather than
silently clamping an authored level or inventing banks.
Reflection/refraction, waves, foam and underwater
appearance remain pending; this is not completion of the water quality gate.

Design water-body data and hydrology interfaces alongside milestones 2–3;
complete rendering and gameplay integration against the material pipeline.

- [x] Separate sampled field/mask data from generation and water tessellation.
  Pure water tests link sampling/hydrology targets without the engine core.
- [x] Query contexts share prepared coverage and copy-on-write flow data.
  Native results report surface/bed elevation and point containment separately
  from the existence of a wet column; coordinates remain terrain-local.
- [ ] Expose world-space gameplay queries through Lua, with terrain transforms,
  cache invalidation and scene lifetime handled by a runtime service.

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
  - [x] Publish the connected water surface as native transparent geometry, with
    stable body IDs, shared ownership, prepared-data loading and atomic updates.
  - [x] Add configurable water appearance and per-vertex depth shading before
    reflections/refraction. Preserve prepared bathymetry and gameplay queries.
  - [x] Clip shore triangles to ground intersections and share the interpolation
    with queries. Warn when bounded lake banks cannot contain the chosen level.
  - [x] Restrict lake coverage to its centre-selected connected basin, preserving
    authored river paths and ocean coverage. Retain connectivity in prepared
    results for mesh/query parity and avoid frame-time flood-fill work.
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

Partly implemented. Scatter constraints (water, slope, terrain and painted
exclusions, protected areas, roads, building footprints) and stable scatter
identities with render grouping, LOD selection and collision policy are done and
tested. They are not yet consulted by the scatter solver, which still tests water
and exclusions inline, so the two can disagree until they are wired.

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

Partly implemented. The cooked heightfield format is done and tested, including
a hostile-input-safe deserializer, a content digest and per-cause staleness
reasons, and `TerrainCookNode` routes it through the existing cook graph: the
recipe is the source, the generator version and input fingerprint are the
importer inputs, and the palette is recorded as a dependency. `TerrainLod`
selects a level per chunk by distance to the chunk's NEAREST point and builds a
reduced mesh with exact power-of-two cell steps, normals recomputed at the
reduced resolution, and skirts at boundaries so a level mismatch is filled by
geometry instead of showing sky. Cross-chunk edge stitching, camera-driven
streaming, cross-system LOD coordination, the renderer audit and the performance
qualification are not implemented.

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
