# Finite terrain authoring qualification

Public workflow: `tools/package-store/templates/docs/content/terrain.html.twig`.
Remaining scope: [terrain-plan.md](../terrain-plan.md).

## Implemented ownership

- `TerrainRecipe` owns parsing/defaults and validation; the reflected Terrain3D
  component stores the authored recipe and an immutable generated-field handle.
  `TerrainLayer` provides the named, toggleable generation, biome, sculpt,
  protection and exclusion layers; base stages always evaluate before surface
  edits, and layer/stroke order is preserved within a stage.
- `TerrainGenerator` uses pinned FastNoiseLite and replays radial edits over the
  procedural base. Samples and normals are global to a finite heightfield, so
  chunk boundaries agree. Float-resolution validity is checked explicitly.
- `TerrainSamples` provides copy-on-write paged sample storage. A local edit
  detaches only the pages it writes, so an incremental stroke does not copy the
  whole heightfield.
- `TerrainEvaluation` is the single shared evaluation path used by both full
  generation and local replay, so an incremental stroke and a full regeneration
  of the same recipe agree.
- `TerrainUpdate` classifies a recipe change as local or global, computes the
  dirty sample rectangle, widens it through smoothing dependencies, and replays
  from the retained `baseHeights` checkpoint. `TerrainPatch` records local
  before/after samples, palette deltas and an explicit invalidation, and
  coalesces a dragged stroke into one minimal patch.
- `TerrainWorld` prepares a change as replace/tint/remove decisions and publishes
  it behind a staleness gate, installing mesh and static collision together
  without allocations. Untouched chunk entities and their colliders keep their
  identity. `Terrain3DComponent`'s reflected `recipe` binding clears the retained
  field, so generic recipe editing correctly falls back to full generation.
- `TerrainWorld` creates native-only chunk meshes and matching static triangle
  colliders. Scene loading and the editor use this same path. A terrain-owned
  index synchronizes generated visibility without scanning unrelated worlds
  on every frame; scene activation/unload rebuilds ownership.
- `TerrainGenerationCache` weakly deduplicates canonical recipes and validates
  published field structure. Worlds/controllers retain memory, not the cache.
- `EditorTerrainAuthoring` owns drafts, strokes and cancellable generation.
  The workspace commits completed recipes through its existing reversible
  commands, rejecting stale results, and replays the native sample patch so
  terrain Undo/Redo moves the recipe and the visible surface together. Native
  generator data is never saved as source meshes. Structured field commands
  preserve numeric payload precision. Play, Save, Undo and Redo are blocked
  while terrain work is pending, so play never starts on a stale surface.
- `TerrainSurface` shares triangle-consistent sampling and grid-traversal ray
  picking. The editor uses common viewport camera rays and terrain transforms.
  Generated chunks redirect to the authored owner and stay out of the authored
  hierarchy. Framing the owner includes the full generated surface.

## Linux checks, 2026-09-28

Release CLI/runtime/editor built. Focused tests cover generation, edit replay,
protection, invalid data, cancellation, cache structure/ownership/concurrency,
ray queries, mesh/collider parity, transformed physics raycasts, editor drafts,
strokes, stale jobs, Undo/Redo, Save/reload and source/generated separation.
Related scene-document, prefab-component, runtime-object-model, embedded-play
and canonical-schema regression checks passed during qualification.

The `terrain_3d` example validates for Linux and Android target data. It was
visibly rendered with Vulkan at 1280x720; the native editor also opened and
displayed the terrain and Inspector without diagnostics. Synthetic desktop
clicks did not reliably reach this Wayland/X11 session, so interactive brush
feel and dropdown operation still merit user verification. Automated tests
exercise the real controller/workspace paths, not just recipe mocks.

The maintained website Lua-service name check passes (817 references). PHP
template validation and a physical Android/device performance run were not
performed. Capability checking reports additive changes, including earlier
task/database/shadow changes; no unrelated baseline rewrite was made here.

## Linux checks, 2026-09-29 (incremental editing)

The milestone 1 code was committed without a matching schema export, plan
update or public documentation, and left two gates red. Both are now corrected
and the full Release suite was re-run: 357 of 359 tests pass.

- `demi-component-schema-tests` failed because adding `TerrainLayer` and
  `TerrainExclusion` changed the `Terrain3D` recipe defaults without
  regenerating `schemas/components.schema.json`. Regenerated with
  `demi schema export`; the schema is the only generated artifact that changed.
- `demi-editor-android-packaging-tests` failed because the new terrain code did
  not compile for the Android NDK. Two standard C++20 portability defects were
  found and fixed:
  - `TerrainSamples::writeSample` used `std::shared_ptr::unique()`, which was
    deprecated in C++17 and removed in C++20. libstdc++ still provides it, so the
    defect was invisible on Linux, but the NDK's libc++ correctly removed it.
    Replaced with `use_count() != 1`.
  - `Terrain3DComponent` used inline lambdas in its `runtimeFields` initializer.
    Those name the enclosing type while it is still incomplete; Clang rejects
    this and GCC accepts it. Moved to named static member functions defined in
    the `.cpp`, matching the existing convention used by
    `SpriteAnimator2DComponent` and `AnimationStateMachineComponent`.

  All nine terrain translation units and `Terrain3DComponent` were then compiled
  directly with the NDK clang at `-std=c++20` to confirm portability, and
  `demi build apk` progressed past every DemiEngine source file.
- `docs/terrain-qualification.md`, `terrain-plan.md` and the maintained terrain
  page were updated; the page previously stated that brush edits regenerate the
  full heightfield and documented neither the layer system nor the exclusion
  brush, all of which shipped in the same commit.
- The locality decision was a hand-maintained field whitelist inside
  `TerrainUpdate.cpp`, so the generation base could have been extended without
  classifying a new input, which is the seam this milestone warns about. It is
  now `TerrainRecipe::sameGenerationInputs`, declared beside the fields it
  classifies, and `demi-terrain-locality-tests` asserts that every generation
  input forces a full rebuild while every surface-only and layout-only input
  stays local. Writing the test found two real behaviours worth pinning: a
  resolution change is a full rebuild at the new grid rather than a replay onto
  the old one, and a `Protect` edit carrying no snapshot is rejected outright.

Two Release tests still fail, both unrelated to terrain and both predating this
work. `demi-bgfx-profile-callback-tests` reports an unexpected callback scope
population. `demi-package-android-multiplayer-ffa-shooter` is blocked by pinned
libuv v1.52.1 failing to compile against NDK 29 (`pthread_barrier_init` is
undeclared in `thread-common.c`); this affects every Android packaging target,
not terrain, and is a separate third-party/toolchain issue.

## Surface-query measurement

`build/linux-release/demi-terrain-surface-tests` compares the same 250 seeded
rays against an exhaustive triangle reference on a 128-by-96-cell heightfield.
A Release run measured 167.16 ms total for exhaustive queries and 0.464 ms for
grid traversal. All hit/miss and intersection checks passed. This is a scoped
CPU query comparison, not terrain FPS, GPU timing or large-world qualification;
desktop background load and CPU frequency were not controlled.

## Incremental edit measurement, 2026-09-29

`build/linux-release/demi-terrain-edit-benchmarks` compares a single radius-3
raise stroke against full regeneration of the same recipe, at 256 and 512 cells
per side, reporting the median of five iterations on the same process. Both
paths allocate through a counting global `operator new`.

| 256 cells (65,536 samples) | full | incremental |
| --- | --- | --- |
| heightfield work | 9.34 ms | 0.039 ms |
| scene/mesh/collider publication | 16.56 ms | 1.34 ms |
| total | 25.90 ms | 1.38 ms |
| allocations | 5,360 | 1,153 |
| requested bytes | 53,294,282 | 3,301,499 |

| 512 cells (262,144 samples) | full | incremental |
| --- | --- | --- |
| heightfield work | 35.28 ms | 0.063 ms |
| scene/mesh/collider publication | 58.50 ms | 1.49 ms |
| total | 93.78 ms | 1.55 ms |
| allocations | 18,428 | 1,927 |
| requested bytes | 212,956,882 | 3,384,887 |

A local stroke is 18.8x faster at 256 cells and 60.5x at 512, and its cost is
essentially independent of grid size (1.38 ms versus 1.55 ms) while full
regeneration scales with sample count. The stroke reports 47 changed samples,
3,688 bytes of retained local history at both grid sizes, and
`base_evaluations = 0`, confirming the procedural base pass is skipped entirely
rather than recomputed and discarded.

This is a scoped CPU comparison of the engine path on a desktop machine with
uncontrolled background load and frequency scaling. It is a single synthetic
stroke, not interactive brush feel: it does not measure the editor's per-frame
stroke batching, frame-time percentiles while dragging, GPU upload or Android
behavior. Unrelated chunk resources are checked for retained identity by
`demi-terrain-world-update-tests` rather than by this benchmark.

## Milestone 2 work, 2026-09-29

Stable sub-seeds, generator-versioned caches and automatic biome rules are
implemented. Presets, asset palettes and the editor surface for rules are not.

- `TerrainSeed` derives an independent sub-seed per channel from the world
  seed. The channel *name* is hashed rather than the enum ordinal, so inserting
  or reordering channels cannot change an existing channel's value, and the
  channel set already reserves erosion, hydrology and scatter for milestones 3
  and 5. Derivation is splitmix64 over fixed-width integers, so it is stable
  across platforms and compilers. `demi-terrain-seed-tests` pins the values,
  proves channels never collide, and checks that adjacent world seeds diverge
  substantially rather than by a small delta.
- Biome noise moved from the raw world seed to the Landform sub-seed, and
  moisture to BiomePlacement. Both are single shared fields rather than
  per-biome, so adding a biome does not reshape existing terrain. This changes
  the sampled surface for unchanged recipes, so `terrainGeneratorVersion` was
  bumped to 2 and folded into the terrain generation cache key;
  `terrainGenerationCacheKey` exposes the key and
  `demi-terrain-seed-tests` asserts the version is actually in it, so a stale
  heightfield can never be served across an engine update.
- `TerrainBiomeRule` adds automatic biome assignment. Rules evaluate against a
  per-sample `TerrainRuleContext` derived from the finished base surface,
  because slope and water distance cannot be known per point. Water distance is
  a multi-source chamfer transform seeded from every sample at or below sea
  level, with the flat-symmetric 3-4 diagonal step. Where a recipe has no water
  at all the field is left maximally distant rather than zero, so a
  water-distance band cannot accidentally match.
- Highest priority wins, equal priority keeps the earlier rule, and `blend`
  ramps the elevation transition. Painted regions are applied after rules and
  always win. Disabling a biome layer removes its rules exactly as it removes
  its regions. A recipe with no `rules` is bit-identical to before the feature,
  which is asserted directly.
- Rules read the whole field, so they are registered in
  `TerrainRecipe::sameGenerationInputs`; the milestone 1 locality contract then
  forces a full base rebuild when a rule changes, instead of replaying locally.
  This is the first concrete use of that contract and is covered by
  `demi-terrain-rule-tests`.
- Every decision records the rule that assigned it, so a future editor or CLI
  surface can explain a choice rather than leaving the author to guess.
- All four new and changed terrain translation units compile with NDK clang at
  `-std=c++20`, continuing the portability check from the milestone 1 fixes.

Writing the rule tests surfaced two things worth pinning. A rule band must be
chosen against the generated field's actual height distribution: the first
version hardcoded thresholds that Perlin output never reached, so the test
passed vacuously on "no biome assigned" rather than on correct assignment. And
`toJson()` validates, so a rule that is invalid only by validation escapes
before a parse-based rejection check can catch it.

## Milestone 2 second body of work, 2026-09-30

This section records the preset, palette, editor and CLI work that completed
after the 2026-09-29 entry above. No test or build numbers are claimed here: the
coordinator owns the gate runs for this body of work, and this section records
what the source does, not a run that this entry did not perform. The behaviour
below was read out of the implementation and the focused test sources
(`demi-terrain-preset-tests`, `demi-terrain-palette-tests`,
`demi-terrain-cli-tests`, `demi-terrain-rule-tests`, `demi-terrain-seed-tests`),
and the CLI example quoted in the maintained page was captured from a real
`demi terrain explain` run against the `terrain_3d` example recipe.

### Implemented

- `TerrainPreset` is a reusable, versioned `DataAsset` identified by
  `settings.content_type: "terrain_preset"` and referenced by a normal
  `asset://` URI. `loadTerrainPreset` resolves the manifest, checks the type and
  the content type, and `parseTerrainPreset` validates the assembled preset by
  calling `TerrainRecipe::validate()` on a probe built from its own fields, so a
  preset that names an unknown biome or a missing layer cannot be applied.
  Preset rules go through `TerrainBiomeRule::parse`, the same authored parser, so
  a preset cannot express anything an authored rule cannot and both reject the
  same mistakes.
- `terrainPresetGenerationKeys` is the canonical list of what a preset supplies:
  `size`, `resolution`, `chunk_cells`, `seed`, `default_biome`, `biomes`,
  `layers`, `rules`. The fragment is round-tripped through
  `TerrainRecipe::toJson()` rather than assembled field by field, so a preset is
  validated by exactly the code path that applies it. The preset document names
  resolution per axis as `cells_x`/`cells_z`; the recipe it produces uses the
  single `resolution` pair.
- `applyTerrainPreset` replaces generation keys from the preset and copies
  `regions`, `edits` and `exclusions` from the authored recipe. It stamps
  `preset_id` and `preset_version`, then parses the merged document so a malformed
  merge fails at the boundary rather than at generation time. The authored
  document is deliberately not validated on its own first: its strokes only make
  sense against the biomes the preset supplies.
- Provenance is enforced as a pair. `TerrainRecipe::validate` requires
  `(presetId.empty()) == (presetVersion == 0)`, so a half-stamped recipe is
  rejected. `sameGenerationInputs` compares both fields, so applying a different
  preset is classified as a generation change and not a display preference.
- `TerrainPalette` is a `DataAsset` with `settings.content_type:
  "terrain_palette"`. `loadTerrainPalette` and `parseTerrainPalette` are separate
  so a caller holding a `DataDocument` (Lua `Data.load`, a hot-reload snapshot)
  reaches the same rules without re-reading the file. All eleven roles are
  enforced against a fixed vocabulary table that is also the source of the
  "unknown role" error message, so a new role cannot be accepted by the loader
  and missing from its own diagnostic.
- Palette documents store `roles` as an object keyed by role name rather than an
  array of entries, because `DataValue::Object` is key-sorted. Duplicate roles
  are therefore structurally impossible and iteration order does not depend on
  how the file was written. `required_roles` is sorted and deduplicated on load.
- Every `asset://` reference must also appear in the palette manifest's
  `dependencies` array. This is enforced by `validateDataAssets`, not by the
  palette loader, because cooking and packaging follow the manifest. Verified
  directly: removing `asset://terrain/surfaces/grass` from the `terrain_3d`
  example manifest and running `demi validate` produced
  `DATA_DEPENDENCY_UNDECLARED` naming that reference. This is the single most
  likely authoring mistake, because the palette otherwise validates and loads
  cleanly and then ships without its art.
- `EditorTerrainInspector` gained a `Biome rules` section: per-rule ID (editable,
  with duplicate and empty checks), target biome, target layer restricted to
  enabled biome-kind layers, priority, blend, a checkbox plus a min/max pair per
  band, substrate multi-select, and add/remove. An unchecked band erases the key
  rather than writing a null, which keeps saved source free of conventions that
  mean nothing. Rename is safe because nothing outside the rule stores its ID.
- `EditorTerrainAuthoring::ruleMaskPreview` implements the viewport overlay for
  `None`, `Biome`, `Elevation` and `Slope`. It reads the committed heightfield
  only, so the overlay never re-evaluates draft rules and cannot disagree with
  what the author last generated. Elevation does one extra read-only
  `minmax_element` pass over committed heights and normalizes against that
  observed range, yielding 0 for a degenerate range. Slope reads the already
  committed normals. Biome uses the field's own `biomeIndices` and
  `biomeColors`.
- `EditorTerrainAuthoring::needsResizeDecision` correctly ignores rules. Rules
  are grid-independent; protection snapshots are not, and requiring the original
  grid is what the resize decision exists for. This was checked rather than
  assumed.
- `src/cli/TerrainCommands.{h,cpp}` adds `demi terrain inspect`, `demi terrain
  explain` and `demi terrain seeds`, wired in `src/cli/main.cpp` and present in
  `--help`. `explain` takes the assignment from the runtime
  `TerrainRuleDecision` and reports band containment facts against the
  `TerrainRuleContext`; it deliberately does not reimplement the runtime's
  blending weighting, so a `blend` ramp is reported as a contained band rather
  than a fractional score. Progress and diagnostics go to stderr, so
  `--format json` on stdout stays parseable. A scene passed where a recipe is
  expected produces an explicit message naming the `recipe` object.

### Deliberately not implemented

- There is no editor picker, menu item or CLI command that applies a preset.
  `applyTerrainPreset` and `loadTerrainPreset` are called only from
  `demi-terrain-preset-tests`. The maintained page describes the concept and the
  authoring shape and states the absence explicitly rather than implying a UI.
- Palettes are definition and validation only. Nothing scatters or instances the
  roles onto terrain; there is no placement pass, no instancing and no LOD
  selection at runtime. Milestone 6 owns that.
- The example palette in `examples/terrain_3d` references material assets,
  because authoring real mesh art is outside this milestone. The loader resolves
  references without checking asset type, so meshes can replace materials one for
  one without touching the palette, its schema or the loader. This was confirmed
  in the loader rather than inferred.
- The mask preview has no moisture mode. Moisture is built in a transient
  `TerrainRuleContextBuilder` pass over the whole field and is not stored on the
  field, so an overlay would either cost an O(cells) regeneration per frame or
  display a fabricated zero. The maintained page states this rather than leaving
  the absent option unexplained.
- Drainage conditions on rules are absent, and stay so: they need the flow
  accumulation and basin output of milestone 3.

### Bugs found and fixed during this work

- A typo inside a rule was silently ignored. `TerrainBiomeRule::parse` did not
  call `checkKeys`, so `"elevaton"` parsed as a rule with no elevation band
  instead of failing. This is not cosmetic: the editor surfaces parse failures
  verbatim, so a dropped key looks applied to the author while doing nothing.
  `checkKeys` is now called with the exact key set, and
  `demi-terrain-rule-tests` pins the rejection with an `elevaton` typo.
- `schemas/components.schema.json` was stale again after `rules` was added to
  the Terrain3D recipe defaults. Regenerating it also picked up `layers` and
  `exclusions`, which indicates the earlier milestone 1 regeneration was made
  before those two fields were added rather than not made at all. The exported
  default now contains all three.
- The `Terrain3DComponent` inline-lambda portability defect from the 2026-09-29
  entry was still present in the working tree as an uncommitted change and had to
  be carried, not rediscovered: `runtimeFields` used inline lambdas naming
  `Terrain3DComponent` while it was still incomplete, which Clang rejects and
  GCC accepts. It is now `copyRecipe`/`readRecipe`, named static member functions
  defined in the `.cpp`, matching `SpriteAnimator2DComponent` and
  `AnimationStateMachineComponent`. This is the same class of defect as the
  `std::shared_ptr::unique()` removal recorded previously, and it is invisible on
  Linux by construction, so a Linux-only gate can never catch it.

### Design decisions and their rationale

- Presets and palettes are `DataAsset` documents with a `content_type`, not new
  document kinds. Both therefore inherit cooking, packaging, dependency closure,
  `demi validate` coverage, Lua `Data.load` and hot reload from the existing
  data-asset pipeline. A new document kind would have needed a second
  implementation of each of those to say nothing new.
- A preset is a recipe fragment applied once, not a runtime override. The
  recipe stays the single source of truth for generation, so incremental
  editing, the `sameGenerationInputs` locality decision and undo/redo all answer
  the same question they answered before presets existed. An override would make
  each of the three describe a different terrain, and a preset could disagree
  with the terrain it was applied to.
- Strokes are excluded from the preset's key set by construction rather than by
  convention. `terrainPresetGenerationKeys` simply does not contain
  `regions`, `edits` or `exclusions`, so no preset can express them and the
  author cannot lose work to a template.
- The merged document is validated as a whole rather than merging two validated
  documents. A region painted with a preset biome is invalid before the merge and
  correct after it, which is exactly the case a per-document check would reject.
- Biome rules are registered as a global generation input, not a surface-only
  field. They read the finished base surface, so a local replay could not
  reproduce them. `sameGenerationInputs` is where that decision lives, which is
  the first concrete use of the milestone 1 contract by a stage added after it
  was written.
- Only rules on an enabled biome-kind layer participate, and the editor will not
  let a rule target a disabled one. This keeps layer disablement meaning the same
  thing for regions and for rules instead of introducing a second toggle.
- The palette role vocabulary is a closed enum enforced in the loader, and the
  same table generates the error message. An open string role would let a typo
  become a role that silently never scatters, which is the failure mode the
  palette exists to prevent.
- `roles` is an object, not an array. Duplicate roles become unrepresentable
  rather than rejected.
- The engine's data-asset mini-validator implements `type`, `enum`, `minimum`,
  `maximum`, `required`, `properties`, `items` and `reference`, and silently
  ignores `additionalProperties`, `minItems`, `maxItems`, `maxLength`, `pattern`,
  `uniqueItems`, `const`, `propertyNames` and `exclusiveMinimum`. The palette
  schema declares all of the ignored keywords anyway, for editor tooling and for
  external validators, but the authoritative role vocabulary, scale ordering,
  spacing floor and unknown-field rejection are in the C++ loader. Both the
  schema file and the example's schema asset state this, so the two cannot be
  read as if the schema were enforcing more than it does.

## Milestone 2 scattering and presets, 2026-09-30

### Implemented

- `TerrainScatter` places palette roles onto a finished height field.
  `TerrainScatterPlacement` is a description rather than an engine entity:
  deciding where something belongs is a generation concern that has to stay
  testable without a renderer or a world. `TerrainScatterResult::truncated`
  reports the placement ceiling rather than silently accepting it, and
  `scatterableRoles` reports a palette that cannot contribute instead of
  failing generation.
- A role places only where its new optional `biomes` list allows, never in
  water, and never on a cell with exclusion weight. `weight` is a relative share
  of eligible ground, so weight 0 keeps a role available but never selects it.
  `spacing` is enforced with a per-role uniform spatial hash, so one role's
  spacing never rejects another's and the cost stays O(cells x roles) rather
  than growing with the number of accepted instances. `spacing` 0 is continuous,
  unconstrained cover.
- Determinism uses the same guarantee as the generator: a candidate draws from
  the Scatter sub-seed combined with its own cell index and role, never from a
  running counter, so the result is independent of traversal order, worker count
  and repetition. All five seed channels are now actually consumed: landform
  drives biome noise, biome placement drives moisture, and scatter drives
  placement.
- `TerrainRecipe::paletteId` is authored as `"palette": "asset://..."`, and is
  part of `sameGenerationInputs`, so changing a palette forces a full base
  rebuild instead of a local replay. The palette itself is passed into
  `TerrainGenerator::generate` by the caller, which owns the asset registry;
  generation never loads assets itself, and a recipe that names a palette but is
  handed none still generates, because the missing asset is the caller's to
  report.
- All four landscape presets the milestone names are authored in
  `examples/terrain_3d`: mountain range, river valley, rolling hills and
  coastal island. Each was verified by applying it and probing the result with
  `demi terrain explain` on a 15x15 grid, which caught two rules that matched no
  sample at all and a substrate-keyed rule that was unreachable because
  substrate derivation checks `wet` before `rock`.

### Deliberately not implemented

- `TerrainScatterRuntime` now reconciles placements into native children, and
  its tests cover idempotent repeat syncs, in-place movement, cleanup, two roles
  in one cell, missing owners, unresolvable prefabs, scoped release, and that
  both consumers observe one shared service.

  The prefab service is now owned by the composition root and shared rather than
  duplicated. It was a private member of `LuaScriptHost`, which meant terrain
  could not reach it without inverting the dependency direction, and neither the
  editor nor a headless tool had one at all. It is created and configured once
  per application, injected into the host, and forwarded to the `SceneFlow` the
  host owns, so Lua and terrain resolve the same pool; two services would have
  doubled prefab memory for the same asset.

  Placement is wired into both the load and update paths, deliberately after
  their atomic commit. Publication is allocation-free by contract and prefab
  instantiation allocates, so the two cannot share a step, and a scatter failure
  must not discard terrain that is already correct. Scene loading runs on a
  worker and the shared service is mutable state the scripting host also uses, so
  the worker never touches it: `materializeTerrainScatter` runs on the main
  thread in `SceneFlow::poll` before the prepared world is adopted. That ordering
  is the non-obvious part, and a data race here would have been intermittent
  rather than obvious.
- Asset map, licence and compatibility validation remain unimplemented, and the
  example palette deliberately points at placeholder material assets.
- A palette cannot yet override a rule's biome choice. The plan's acceptance
  line is met on the generation side and unmet on the asset side.

### A real hazard found and fixed

`TerrainUpdate.h` had a `struct TerrainPalette` holding biome ids and colours for
a patch tint snapshot. When the asset palette type arrived, two unrelated types
shared one name and the terrain subsystem would not compile. The patch type is
now `TerrainBiomePalette` and its helper is `sameBiomePalette`. Two same-named
types is not a naming nit: it is how a palette of colours and a palette of
assets get silently confused.

## Editor viewport depth range, 2026-09-30

Large terrain lost its distant chunks in the editor viewport, which reads as
"the far parts never load" rather than as a camera problem.

Cause: the editor viewport delegates all visibility to the shared renderer, and
the shared renderer is correct. Terrain chunk bounds are exact per-biome
sub-AABBs, `cullDistance` is left at 0 so distance culling is disabled, and
`EntityLookup` revalidates itself, so none of the usual suspects were at fault.
The actual cause was the depth range. The editor's `alignToFirstCamera` copied
the authored `Camera3DComponent` verbatim, including its gameplay `far_clip`, and
never re-framed. The only code that raised the far plane was `frameBounds`, which
is reachable solely from "frame scene". The engine default and the example's
authored camera both used 500 units, while `examples/terrain_3d` spans
1024 x 512 world units with a corner-to-corner distance near 1145, so everything
beyond 500 was rejected by the sphere test in `SceneVisibility3D`.

Fix: an authored `far_clip` is a gameplay decision about how far the player can
see, while the editor viewport has to frame the content an author can plainly see
in the hierarchy. `EditorSceneViewState` now widens the depth range to cover the
scene bounds without moving the camera, both after aligning to an authored camera
and after framing. The example's own `far_clip` was also raised, because the
gameplay camera genuinely cannot see the far end of its own terrain.

The near plane is now also raised proportionally to the orbit distance. It used
to stay at 0.05 while framing pushed the camera out past 1300 units, giving a
near:far ratio above 25,000:1, at which distant geometry begins to quantise. That
is a separate symptom from the missing geometry, and fixing the far plane alone
would have left it.

## Landforms and biomes, 2026-09-30

Milestone 2 required biome appearance to be separable from landform shape. A
`TerrainBiome` had five elevation fields and a `color` in one struct, and
`baseSample` read the elevation from it, so recolouring a surface meant
reshaping the terrain.

`TerrainLandform` now carries `base_height`, `height_variation`,
`feature_size`, `roughness` and `octaves`; `TerrainBiome` carries a landform
reference, a `color` and an optional `material`. The recipe names a
`default_landform`. Several biomes may share one landform, which is where the
sharing actually pays: elevation is sampled per landform, so a shared shape costs
one noise instance instead of one per biome.

The non-obvious part was the weight fold. Region strokes paint a biome, so the
weights that come out of `baseSample` are per biome, but the shape they drive is
per landform. Summing those per-biome weights directly would let two biomes
sharing a landform count it twice and warp the blend toward that shape. The
weights are therefore folded onto landforms first, and a test asserts a field
using one shared landform through three biomes comes out at exactly that
landform's height.

A biome that still carries elevation is rejected by name, with instructions,
instead of being ignored. That is deliberate: a silently dropped height field
would flatten a terrain that used to have relief, and would look like a
generation bug rather than an authoring error.

The example, all four presets, the editor fixtures and the terrain tests were
migrated. The inspector gained a Landforms section and the biome editor was
reduced to tint plus a landform choice. The CLI reports landforms and biomes as
two tables, which is the clearest way to see that they are two things.

## Generation pipeline and shared data-document readers, 2026-10-01

The milestone 3 to 7 modules existed and were tested, but nothing ran them.
`TerrainPipeline` now composes them in a declared order and reports what
actually ran, so a caller asks instead of inferring. A preview omits erosion
and says so; `terrainStageRan` answers whether a stage ran, so a stage that did
not run is never mistaken for a mask full of zeros.

Masks are derived by the code the rules already match against, not a second
derivation. That is the whole point: a second implementation of slope, moisture
or water distance is how a rule and a scatter end up disagreeing about what a
cell is. One sea level is computed once and used by every stage, for the same
reason.

Two ordering problems were found and handled rather than papered over. The
generator produces landform, surface and sculpting in one pass, so the derived
stages necessarily follow it, and the declared stage order was corrected to say
so instead of describing an order the pipeline did not use. More seriously,
erosion applied after sculpting would wash away an author's strokes. The
pipeline cannot yet replay strokes onto an eroded base, so erosion is refused
outright, and reported as not run, when the recipe has sculpt strokes. Losing
sculpting silently is worse than not eroding.

**Shared readers.** The int-versus-double trap in `DataValue` was copy-pasted
into four document parsers, and it has already produced a real
`std::bad_variant_access`. `DataValueRead` now holds the handling once,
along with the message conventions: a rejection names its location, required
fields say so by name, and a range is printed in whole numbers where it is a
whole number rather than as `0.000000`.

**File sizes.** `TerrainCook.cpp` had reached 915 lines because the binary
format and the orchestration lived together. They change for different reasons,
so they were split: `TerrainCookFormat` owns the byte layout, the
bounds-checked reader and the digest; `TerrainCook.cpp` owns cook, load and
staleness. The re-cook self-check that ran on every load moved to the test,
which asserts the same equality without serialising the payload a second time
in production.

## Graph authoring integration, 2026-10-04

The editor now has a shared Modules palette beside Inspector. HUD module drops
use the existing authored HUD document, placement and Undo/Redo paths. Terrain
graphs use a separate Stage tab, with imnodes providing canvas interaction and
native graph metadata providing modules, typed ports and parameters.

`TerrainGraph` owns parsing, link validation, cycle detection and dependency
order. Incomplete editor drafts use the same validator without requiring the
final output and all inputs. `TerrainGraphExecutor` owns evaluation and retained
per-node results; the editor does not interpret terrain nodes itself. Generation
consumers dispatch graph recipes through this executor. Only dependencies of
the selected output execute. Positions are authored layout, not cache inputs.

The graph output retains an unpainted checkpoint. Biome paint changes labels
without replacing graph heights; sculpt/protection/exclusion edits replay after
that checkpoint. Input fingerprints invalidate scatter without rerunning
unchanged height sources. Palette snapshots are resolved before editor jobs
start, and initial-scene scatter uses the same prefab service as Lua spawning.

Review fixes also include recipe provenance in cooked terrain keys, final
normal recomputation after erosion/water/sculpting, cancellation checks in those
passes, reported scatter failures and distinct ImGui IDs for rule controls.
The graph canvas edits river paths as XYZ waypoint rows, not raw JSON.

The first integration check passed native graph tests, graph document history,
HUD authoring/drag tests, example validation and headless graph-example startup.
A visible check exposed incorrect Inspector tab/window nesting, which was
replaced with content-only Inspector functions and a UI regression check.
Final qualification commands and results are recorded below after verification.

## Editable terrain assets and cooked loading, 2026-10-04

Terrain authoring now has a first-class `Terrain` asset source and
`terrain_heightfield` importer. Scenes store `Terrain3D.asset` references.
Inline `Terrain3D.recipe` explicitly opts into procedural generation; empty
components create no surface. Cross-field validation and exported schema
constraints reject an asset combined with a recipe.

`TerrainAssetSource` owns the source contract, `TerrainAssetPayload` owns the
versioned prepared format, and `TerrainAssetStorage` owns persistent cache I/O
and immutable decoded-field reuse. Asset payloads retain the rendered field,
pre-edit checkpoint, exclusions, palette/placements and water data without the
source recipe or graph. Prepared asset loading never invokes generation.
CLI development execution and editor workflows prepare source caches before
loading; cooked projects use their shipped binary sources directly.

`EditorTerrainAssetDocument` uses the existing authored JSON document/history
and conflict-aware save service. The standalone Terrain stage reuses shared
world loading, graph controls and brushes. Scene placements retain references
when the asset changes. Source extraction and placement are ordinary scene
commands, not raw JSON editing. Brush previews remain in memory; persistence
occurs on asset Save rather than encoding a whole payload every mouse update.

Terrain cooking uses the ordinary asset cook graph and output audit. The cooked
manifest references a `.terrain.bin` payload. Publishing stages/verifies the
binary and manifest before replacing prior outputs. Old recipe copies are
removed only if tracked by the preceding cook manifest and still hash-matching;
untracked files are retained and reported. Locked package sources keep their
original lock declarations; cooked substitutions are checked against the cook
manifest's output hashes and source-package provenance.

Qualification:

- Release build of the affected CLI, editor, runtime and focused test targets.
- 59 selected CTest checks passed, covering terrain kernels/cache/history,
  asset source/payload/cook, locked-package integrity, editor asset authoring,
  shared placements, runtime asset ownership and affected engine workflows.
  This is a scoped gate, not a claim that every repository test was run.
- `demi validate examples/terrain_graph_3d --platform android` and the existing
  `terrain_3d` project validation passed. Cooked terrain data also validates for
  Android. No physical-device qualification was performed in this delivery.
- Cooked `terrain_graph_3d` into
  `build/terrain-ship-Rj3WZE/cooked`; it contains the `.terrain.bin` and asset
  manifest, with no `.terrain.json`. Validation and a three-frame headless
  runtime smoke passed.
- Built an audited Linux bundle at
  `build/terrain-ship-Rj3WZE/linux-bundle`; its packaged launcher completed the
  same headless smoke. The content report lists two asset files and no preview
  cache or terrain authoring source.
- Opened the terrain source directly using the Release editor's `--open`
  option under SDL/X11 and Vulkan for 120 frames; no ImGui or runtime errors
  were logged. Interactive drag routing has synthetic ImGui coverage; this
  smoke is not a large-landscape rendering/FPS qualification.
- `git diff --check` passed. Website templates were updated, but PHP template
  rendering tests could not run because PHP is not installed on this machine.

## In-scene shared authoring and graph configuration (2026-10-05)

Generation controls now live in `EditorTerrainGraphSettings`, displayed beside
the graph canvas. The Terrain component Inspector owns brush controls and asset
actions. Conventional recipes remain editable there without being silently
converted to a connected graph. The settings include grid/seed, presets,
landforms, biomes and their references, rules, palette and surface layers.
Material selectors explicitly identify the pending terrain rendering integration.

Selecting a scene placement binds `EditorTerrainAssetDocument` in place, retaining
the scene world and its transforms. Apply saves the shared asset source and
prepared field; scene JSON retains only asset references. Source drafts and graph
history use asset identity, while picking/publication use the selected entity.
Discarding a draft on one placement cannot revive it from another placement's
cache. Ordinary scene changes preserve pending asset edits, and placement
transform history stays separate from terrain source history. Editing a different
asset with unsaved changes is explicitly refused rather than losing those edits.

`TerrainWorldBatch` stages geometry, scenery, ownership and prefab bookkeeping
before publication. It retains untouched mesh buffers, collider references and
ownership storage, and reads prior chunk bounds from the retained heightfield,
not an authoring recipe that cooked assets do not carry. Prefab bookkeeping is
forked only when creating or releasing instances. Scene previews now provide a
retained prefab service so scenery exists on initial load and follows local
height/exclusion edits. The optional isolated asset stage parks and restores the
scene's prefab state as well as its world.

Qualification:

- Release CLI/editor and affected test targets built successfully.
- 20 selected CTest checks passed in 8.35 seconds. These cover editor graph UI,
  shared source edits/drafts/history, scene authoring, runtime prefab/scatter
  behavior, cooked asset loading and transactional publication. This is a scoped
  gate, not a full repository test run.
- Added explicit cases for a stale second placement leaving all owners unchanged,
  cooked terrain without a prior source recipe, scenery movement retaining live
  state, exclusion removal/Undo, and pending drafts across placements and stages.
- `terrain_graph_3d` validates for Android, `terrain_3d` validates for Linux, and
  the graph example completes a three-frame headless runtime smoke.
- Synthetic ImGui coverage includes a narrow settings pane and conventional
  recipes. No new interactive GPU or physical Android qualification was run.
- `git diff --check` is clean. PHP is unavailable locally, so website template
  rendering tests were not run. Website deployment is outside this change.

## Graph navigation and landscape probe

Graph navigation now supports cursor-anchored wheel zoom and a 100% reset. The
canvas retains logical node coordinates; widget layout, pins and link hit areas
scale together through the existing ImNodes renderer. Recipe settings use a
floating card with a fixed collapse header and scrollable content. Collapsing it
does not reserve a blank column. Zoom/pan are transient view state, not recipe
edits. Synthetic input tests cover wheel routing, cursor anchoring, repeated
20%-250% navigation without source drift, settings collapse and zero ImGui layout
errors. No new GPU interaction qualification is implied by those checks.

The graph example now has 13 nodes and 17 links on a 128-by-128-cell grid:
mountains, foothills and lowlands, drainage-fed erosion, five biome definitions
with four rules, and connected lake/river results. A native generation test
checks relief, biome diversity and retained water data. This adds no terrain PBR
shader or visible water; those integration gates remain open.

The Release editor and affected test targets built successfully. Eight selected
CTest checks passed, covering graph navigation/UI, graph generation, asset and
scene authoring regressions. The expanded example validates for Linux and
Android and completes a three-frame Linux headless smoke. `git diff --check` is
clean. Interactive GPU navigation and physical-device tests were not run in this
change; PHP website rendering tests remain unavailable locally.

## Remaining limits

This is finite heightfield terrain. Local strokes are incremental, but changes to
the seed, size, resolution, chunk size, biome generation parameters or a biome
rule still trigger a full regeneration, and background generation is followed by
main-thread scene/mesh publication. Graph recipes reuse unchanged node results
during regeneration. Large recipes can still hitch during publication and
GPU/physics upload. Biome tints are discrete triangle groups, not blended
material layers. Erosion and finite water authoring are native graph operations;
the water data is not yet a rendered reflection/refraction system. Cooked-field,
LOD and streaming helpers still need shipping/render-path qualification.
Volumetric caves remain a separate representation decision.

Preset application is reachable from the Terrain Graph settings, and the command
line can apply presets to inline recipes. A palette is consumed at generation
time and instantiated on load and update. What
is still missing is a high-level runtime override API for swapping a preset or
palette asset while the game is running. Editor palette changes use resolved
input snapshots and regeneration; presets are applied through the Terrain Graph.

Protection freezes absolute samples and requires the original grid. Resizing
requires an explicit keep/clear decision; protected grids cannot be silently
remapped. Independently authored scene objects are not automatically moved to
follow regenerated land. Terrain-scattered placements are reconciled against the
edited surface; existing instances keep their identity and live state when moved.

## Compact strokes and bounded full replay

The shared `TerrainBrushStroke` codec stores adjacent equal brush settings once
with ordered `points`. Single radial operations keep `center`; protection
snapshots remain separate. Expansion preserves every stamp and native replay
order, including repeated points and overlap strength. Runtime parsing expands
these source groups into the existing native stamp values; native stamp memory
and very long editing histories are not eliminated by this representation.
Editable raster/tile layers remain a separate authoring follow-up.

The editor appends points without rescanning earlier points and resolves layer
metadata once per pointer stroke, rather than parsing the entire growing recipe
at every stamp. The graph's compaction action changes its draft; Generate/Save
apply it through normal history. `demi terrain compact <source> [--write]` accepts
a recipe or terrain asset source, defaults to preview only, verifies native
operation equality and uses the source-preserving atomic writer. Reimport the
registered manifest after CLI writes.

`TerrainBrushBounds` owns conservative sample conversion for full and local
replay. Full sculpt regeneration visits each brush footprint, not the whole
heightfield per stamp. Snapshot smoothing, protection, cancellation and progress
retain their contract. A reference test compares mixed ordered edits against
the original full-grid algorithm, including borders, fractional centers and
off-grid brushes.

Measured workload: the edited `terrain_graph_3d` landscape, 2000×2000 world units,
500×500 cells, 251001 samples, 2285 sculpt stamps and 474 paint stamps. Compaction
changes 729118 source bytes / 32654 lines to 248600 bytes / 11636 lines, using
nine sculpt groups and two paint groups. All generation settings and points are
retained. The original source and manifest have temporary recovery copies under
`/tmp/demi-landscape-original-01a10dba.json` and
`/tmp/demi-landscape-manifest-original-01a10dba.json`; these are not durable backups.

Release CLI probe, run sequentially without builds or tests in parallel:

```sh
TIMEFORMAT='elapsed_s=%R'
time ./build/linux-release/demi terrain explain \
  <(jq '.recipe' examples/terrain_graph_3d/assets/terrain/landscape.terrain.json) \
  --at 1040,985 --format json
```

Before bounded replay: 16.206 seconds. After: 4.400 seconds. The probe's complete
JSON output is byte-identical. These are single desktop wall-time samples, not
medians, isolated surface-stage timings or GPU/frame-time measurements.

Ten focused release tests pass: terrain generator/layers/update/locality/CLI,
editor terrain authoring/workspace/history and viewport/2D scene interaction.
The edited graph example and `terrain_3d` validate without diagnostics. Viewport
regressions cover first-click selection before dock keyboard focus and exclusive
translate/rotate/scale drag ownership across other objects, release, cancellation
and focus loss. These model-level interaction tests do not replace manual
desktop pointer qualification.
