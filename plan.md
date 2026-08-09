# DemiEngine Developer Experience Roadmap

## Purpose

DemiEngine should let a developer create an ordinary game by adding project
data, assets, and Lua code—not by editing `src/demi`. This roadmap starts from
the migrated SDL3/bgfx engine and prioritizes the workflows developers repeat
across visual novels, platformers, top-down games, isometric games,
lightweight 3D games, and multiplayer games.

This is the authoritative forward plan. Completed renderer-migration history
lives in `docs/bgfx-migration.md`; current supported behavior lives in
`docs/capabilities.md`.

## Current Foundation

The engine already provides:

- deterministic JSON projects, scenes, prefabs, HUDs, assets, and saves;
- Lua lifecycle functions and game-facing services;
- metadata-driven components, runtime entity creation, and scene ownership;
- responsive tree UI, themes, localization, input focus, and touch controls;
- stable 2D rendering, Box2D physics, tilemaps, navigation, and animation;
- isometric placement and pathfinding;
- lightweight bgfx 3D rendering and Jolt physics;
- audio mixing, animation state machines, particles, materials, and shaders;
- game-facing networking and dedicated headless runtime support;
- Linux and Android runtime paths, CLI validation, cooking, packaging, replay,
  and profiling.

The next work is primarily about making those systems easier to discover,
compose, author, diagnose, and ship.

## How To Execute This Roadmap

The numbered steps describe dependency order, not one enormous branch per
step. Each step is divided into vertical slices. A slice is complete only when
it includes all of the following artifacts where applicable:

1. an engine-facing contract or project-local package contract;
2. versioned source data and schema changes;
3. parsing and validation with stable diagnostic codes;
4. runtime behavior with explicit ownership and teardown;
5. Lua bindings and generated/checked stubs;
6. CLI/editor consumption of the same contract rather than copied logic;
7. focused unit tests, failure injection, and a headless integration test;
8. one reference project using the public path;
9. documentation and capability-gate updates;
10. Linux validation plus Android validation when the capability is available
    there.

Use these status values in future edits to this document:

- **Planned:** no stable public contract exists yet.
- **In progress:** at least one vertical slice is being implemented.
- **Experimental:** usable by a reference project but not yet compatibility
  protected or fully failure-tested.
- **Stable:** documented, compatibility-gated, and covered by the required
  tests.

Do not mark an entire step stable because its happy-path API exists. Its
failure behavior, lifecycle ownership, migration, tooling, and reference
scenario are part of the feature.

### Current priority bands

| Priority | Steps | Reason |
|---|---:|---|
| Now | 1–3 | Reduce the cost of starting a project and unblock content/UI-heavy games such as visual novels. |
| Next | 4–7 | Turn stable engine primitives into reusable gameplay and content workflows. |
| Later | 8–9 | Build visual authoring and release tooling on contracts proven by the earlier steps. |
| Continuous | 10 | Add measurement and failure coverage during every earlier step, then complete the production-wide view. |

## Roadmap Rules

Every step follows these rules:

1. Examples expose reusable gaps; they do not permanently work around them.
2. Game policy stays in Lua packages. C++ owns stable platform, rendering,
   physics, asset, serialization, and tooling services.
3. Game code and durable data remain independent of SDL3, bgfx, Box2D, Jolt,
   miniaudio, and transport implementation types.
4. Public APIs update schemas, validation, Lua stubs, documentation, migration
   behavior, and focused tests together.
5. Linux and Android consume the same authored gameplay data and scripts.
6. Each feature is proven by a small reference scenario and an automated
   failure or edge-case test.
7. New abstractions must remove repeated developer work observed in at least
   one real scenario.
8. Generated and cooked files never become the only source of authored state.

## Step 1 — Start, Run, and Diagnose a Project

The first five minutes should not require copying an existing example or
knowing the repository layout.

**Status: implemented.** Project scaffolding, environment diagnosis,
project-local LuaLS support, project smoke testing, and guarded watch-mode
reload now form one supported CLI workflow. Project-manifest edits deliberately
request a restart; Lua, scene, HUD, material, shader, texture, and other asset
source edits retain the last good runtime state when preparation fails.

### Delivered workflow

- New projects come from seven checked-in, validated templates without copied
  generated output or example-specific state.
- `demi doctor` reports project, toolchain, graphics selection, writable data
  locations, Android SDK/NDK/Java, and stale cook-output problems with stable
  diagnostic codes.
- `demi new`, `doctor`, `run`, and `test` accept the documented project paths
  and support automation-friendly diagnostics.
- Watch mode assigns source generations at the filesystem boundary, validates
  changes before activation, and uses prepared scenes and candidate renderers.
- Failed Lua, scene, HUD, material, shader, texture, or other asset edits retain
  the last valid live state and report why the edit was rejected.

### Ownership and dependency boundaries

- A new `src/cli/project` module owns template discovery, parameter
  validation, destination planning, and scaffolding. It may depend on
  filesystem and diagnostics code, but never on the running renderer.
- A `ProjectTemplateCatalog` is a plain read-only catalog of checked-in
  templates. A `ProjectScaffolder` performs a two-stage plan/commit operation
  so partial projects are not left after failure.
- A new `src/cli/doctor` module owns environment checks. Each check returns the
  shared `Diagnostic` value; checks do not print directly or terminate the
  process.
- A runtime `ReloadCoordinator` coordinates existing scene, script, asset,
  material, and UI owners. Each subsystem prepares a replacement and supplies
  commit/rollback behavior. `RuntimeApp` only schedules the transaction at a
  frame boundary.
- File watching remains platform infrastructure. It reports normalized paths
  and change generations; it does not parse assets or decide reload policy.

### Proposed command contracts

```text
demi new <directory> --template visual-novel [--name "My Story"]
demi new <directory> --template lightweight-3d --dry-run
demi doctor --project <project> [--platform linux|android] [--format text|json]
demi run linux --project <project> --watch
demi test --project <project>
```

`demi new` must reject a non-empty destination unless an explicit future
merge mode is designed. It must never overwrite an authored file. `--dry-run`
returns the planned file list and substituted values without writing.

`demi doctor` returns non-zero only for requirements that block the requested
platform. Missing optional media, SVG, or networking support is a warning when
the project does not use it and an error when reachable project data does.

Watch mode uses one monotonically increasing change generation. A burst of
events is coalesced, dependencies are resolved through `AssetRegistry`, and a
new generation may supersede preparation of an older generation. Commit
occurs only between update/render frames after every participating subsystem
has prepared successfully.

### Deliverables

1. Add `demi new <directory> --template <name>` with maintained templates:
   - `blank-2d`;
   - `visual-novel`;
   - `platformer`;
   - `top-down`;
   - `isometric`;
   - `lightweight-3d`;
   - `networked`.
2. Make every template a normal source project with versioned data, a minimal
   Lua entry point, input actions, one scene, tests, and no generated files.
3. Add `demi doctor --project <project>` to check tools, optional libraries,
   renderer support, writable paths, Android prerequisites, assets, and stale
   cooked output with actionable diagnostics.
4. Make `demi run`, `demi test`, `demi validate`, `demi cook`, and platform
   builds use consistent project selection and diagnostic formatting.
5. Add `demi run --watch` for safe Lua, HUD, scene, material, shader, and asset
   reload. A failed reload keeps the last valid state running and reports the
   exact source error.
6. Generate a project-local Lua language-server configuration and point it at
   the checked engine stubs.
7. Document one short create-to-run path and one create-to-package path.

### Implementation slices

#### 1A. Template manifest and scaffolder

1. Define a small template manifest containing template ID, supported
   dimensions/platforms, source files, substitutions, and post-create
   validation command.
2. Store templates under a checked-in top-level `templates/` directory; do not
   embed them as string literals in CLI code.
3. Implement destination canonicalization and path-containment checks before
   any write.
4. Write to a sibling temporary directory, validate the generated project,
   then rename into place. Clean the temporary directory on every failure.
5. Add golden-file tests for deterministic output and line endings.

#### 1B. Environment doctor

1. Add independent checks for CMake/compiler/Ninja, shaderc, renderer/API
   availability, optional media/SVG/network dependencies, writable runtime
   directories, Gradle/JDK/SDK/NDK, signing inputs, and project reachability.
2. Give each result a stable code such as `DOCTOR_ANDROID_SDK_NOT_FOUND` and a
   concrete remediation command or path.
3. Support JSON output without embedding terminal formatting in diagnostics.
4. Reuse actual build/package configuration discovery rather than maintaining
   a second list of guessed requirements.

#### 1C. Transactional reload

1. Establish baseline reload support for Lua modules and HUD documents.
2. Add scene and prefab preparation using the existing scene-flow staging
   model.
3. Add texture/material/shader replacement using generation-checked GPU
   handles; old resources remain alive until successful commit.
4. Rebind dependencies, revalidate references, and emit one structured
   `reload_succeeded` or `reload_failed` event per generation.
5. Preserve explicit runtime state only where a subsystem defines a migration
   rule. Never memcpy or implicitly retain unknown state across reload.

### Failure and edge-case matrix

- destination exists, is non-empty, read-only, or contains a symlink escape;
- template source is missing, malformed, or refers outside its root;
- scaffolding fails halfway because storage is full or permission changes;
- doctor runs without Android tooling, without a display, or under CI;
- files are saved through atomic rename, truncated then rewritten, or deleted;
- one reload generation arrives while the previous one is preparing;
- Lua compiles but throws in `on_start` after preparation;
- a material reload succeeds while one of its textures fails;
- an entity or UI node removed by reload is still referenced by a callback;
- a scene unload and a watched scene edit occur in the same frame;
- repeated successful and failed reloads show no resource growth under
  ASan/LSan and GPU/resource-handle counters.

### Reference migration

Create every template from the catalog in a temporary directory during CTest,
validate it, run three headless frames, and compare its public capability use
against the checked manifest. Existing examples remain richer probes; they do
not become the implementation backing templates.

### Done when

- A new developer can create and run each template with two commands.
- Every generated template validates and passes a headless smoke test in CI.
- Missing SDKs and optional dependencies are reported before a long build.
- An invalid watched edit never destroys the last working runtime state.

## Step 2 — General Game Data and Content Authoring

Games need dialogue, quests, items, characters, encounters, and balance data
without inventing Lua file loaders or adding engine components.

**Status: implemented.** `DataAsset`, immutable `DataDocument` snapshots,
schema validation, revisioned ownership, deterministic Lua `Data` queries,
hot-reload events, and optional pure-Lua content packages now form the general
game-data workflow. `examples/main_menu_animated` is the schema-backed runtime
and cooked-content probe.

### Architectural model

- `DataAsset` is an asset-pipeline concept, not an entity component. It belongs
  in `src/demi/assets` beside other manifest-backed source assets.
- `DataDocument` is an immutable backend-neutral value tree containing null,
  boolean, number, string, array, and object values. It owns no Lua references
  and no live filesystem handles.
- `DataAssetStore` owns validated loaded snapshots, dependencies, revisions,
  and scene/resource-group references. It participates in existing resource
  lifetime ownership.
- `Data` Lua bindings convert a snapshot into ordinary Lua values at the
  boundary. Lua cannot mutate the stored canonical document.
- Optional game packages such as dialogue or inventory consume `Data`; they do
  not live in the asset loader and the asset loader knows nothing about their
  rules.

This keeps parsing, validation, lifetime ownership, and gameplay policy as
separate reasons to change.

### Source format

```json
{
  "format_version": 1,
  "id": "asset://story/chapter_01",
  "type": "DataAsset",
  "source": "chapter_01.json",
  "importer": "json_data",
  "importer_version": 1,
  "source_hash": "fnv1a64:...",
  "dependencies": ["asset://characters/mira"],
  "settings": {
    "schema": "asset://schemas/dialogue",
    "content_type": "dialogue_chapter",
    "tags": ["chapter:1", "locale:neutral"]
  }
}
```

The source document also carries `format_version`. Schema references resolve
through stable asset IDs and may constrain references, required keys, numeric
ranges, enums, and array items. Engine validation does not execute game Lua.

### Proposed Lua contract

```lua
local chapter, error = Data.load("asset://story/chapter_01")
if chapter == nil then
  Debug.log(error.code .. ": " .. error.message)
end

local items = Data.query({
  content_type = "item",
  tags = {"shop:forest"}
})

local revision = Data.revision("asset://story/chapter_01")
```

`Data.load` returns a snapshot. A later hot reload does not mutate tables
already held by scripts; it increments the revision and emits
`data_asset_reloaded`. Callers opt into reading the new snapshot. This avoids
mid-callback mutation and makes save/replay behavior explicit.

### Deliverables

1. Add versioned `DataAsset` manifests for arbitrary JSON-shaped content with
   stable `asset://` IDs, dependency tracking, optional schemas, validation,
   cooking, packaging, and hot reload.
2. Add a narrow read-only Lua API:

   ```lua
   local chapter = Data.load("asset://story/chapter_01")
   local sword = Data.load("asset://items/iron_sword")
   ```

3. Preserve JSON arrays, numbers, booleans, strings, and nested objects during
   Lua conversion. Reject unsupported or cyclic values with diagnostics.
4. Add schema references and cross-asset reference validation so content
   errors are caught by `demi validate`, not during gameplay.
5. Add deterministic content queries by stable ID, tag, and declared type.
6. Add project-local Lua packages for common data-driven state:
   - flags and variables;
   - conditions and simple expressions;
   - quests/objectives;
   - inventories and item definitions;
   - dialogue node traversal.
7. Keep those packages optional and composable; do not turn their game rules
   into mandatory C++ components.

### Implementation slices

#### 2A. Manifest, decoding, and validation

1. Register `DataAsset` and schema assets with the importer registry.
2. Parse source JSON once into `DataDocument`; report JSON pointers for nested
   errors.
3. Validate the source version and optional declared schema before inserting
   it into the store.
4. Extract declared `asset://`, `prefab://`, `scene://`, and localization
   references through schema metadata rather than scanning arbitrary strings.
5. Include schemas and referenced assets in dependency cooking and packaging.

#### 2B. Runtime store and Lua boundary

1. Add immutable revisioned snapshots and reference-count them through scene
   or persistent resource groups.
2. Convert objects and arrays while preserving array indexing and distinguishing
   an empty object from an empty array.
3. Define numeric behavior explicitly: JSON integers within Lua's exact range
   remain integers; other finite numbers become Lua numbers; non-finite values
   are rejected.
4. Impose configurable document depth, element-count, and byte limits before
   allocating untrusted/package data.
5. Add reload events containing stable ID, old revision, new revision, and
   affected dependents.

#### 2C. Optional content packages

1. Define packages as ordinary Lua modules plus data schemas and documentation.
2. Keep flags/conditions as explicit state passed into evaluation functions,
   not hidden globals.
3. Give dialogue, quest, and inventory nodes stable IDs so saves store IDs and
   state rather than copied definitions.
4. Return structured results/events such as `quest_completed` or
   `dialogue_choice_selected`; packages never directly manipulate a specific
   HUD layout.

### Failure and edge-case matrix

- missing, duplicate, cyclic, or wrong-type asset/schema references;
- invalid UTF-8, deeply nested documents, oversized arrays/strings, and number
  overflow;
- object keys that look numeric versus real JSON arrays;
- explicit `null` versus absent fields and defaulted schema values;
- schema version newer than the engine or data version older than supported;
- a reload changes content type, removes the current dialogue node, or breaks
  a saved stable ID;
- two additive scenes load the same data asset and one unloads;
- loading is cancelled during scene preparation or fails after dependencies
  were acquired;
- packages containing malicious paths or references outside the project;
- deterministic serialization/query ordering across repeated runs.

### Test plan

- focused `DataDocument` parse/conversion tests for every JSON type and limit;
- schema tests reporting exact JSON pointers and diagnostic codes;
- scene/resource ownership tests for shared, cancelled, and failed loads;
- package export/import and incremental-cook dependency tests;
- Lua contract tests proving snapshots are immutable and arrays round-trip;
- a save migration test where a stable dialogue/item ID is renamed through an
  explicit migration rather than silently guessed.

### Done when

- A developer can add a new character, item, quest, or dialogue chapter
  without changing C++ or embedding a large table in a gameplay script.
- Data assets validate, reload, cook, package, and save stable references.
- Malformed schemas, missing references, and incompatible reloads have focused
  regression tests.

## Step 3 — Production Text and Dynamic UI

This step provides the general-purpose text and runtime UI capabilities needed
by visual novels, RPGs, strategy games, settings screens, inventories, quest
logs, editors, and lobbies. It does not add an engine-owned dialogue widget,
inventory screen, quest screen, or other genre-specific presentation.

**Status: in progress.** The 3A/3C foundation is implemented: Unicode grapheme
segmentation, immutable wrapping/alignment/ellipsis results, selection/hit
geometry, bounded caching, rich-text validation, renderer integration,
generation-checked transactional mutations, runtime subtree cloning, bounded
uniform and cached variable-height virtual ranges, locale reapplication,
lifetime-bound tweens, Lua contracts,
the dynamic `ui_showcase` probe, grapheme-safe caret/selection editing, and SDL
IME composition with explicit commit/cancel behavior are covered. Project-
authored UI prefabs now add validated typed parameters, defaults, nested
instances, deterministic prefixed IDs, cycle/path-traversal protection, and
transactional failure through the same retained-tree path. Backend-neutral
typed events now cover value changes, focus, submit/cancel, independent
pointer enter/exit and capture, press/release, drag/drop, and scrolling, with
Lua node callbacks and event-bus channels. Hidden, disabled, and removed
subtrees deterministically cancel capture, focus, and active drags. A
backend-neutral accessibility snapshot now derives semantic roles, hierarchy,
labels, descriptions, values, states, and safe canvas bounds from the same
retained tree, including hidden/decorative filtering and inherited disabled
state.
Complex-script shaping, font-fallback atlas pages, row-node recycling with
interaction-state reset, native accessibility bridges/actions are still required before
this step meets its full done criteria; the text adapter reports incomplete
shaping instead of silently claiming correctness meanwhile.

### Scope boundary

- The engine owns text measurement, layout, rendering inputs, node lifecycle,
  focus, events, localization, animation, clipping, and accessibility.
- Games own screen composition, visual hierarchy, content flow, and gameplay
  meaning. A button remains a button; the engine does not decide that it is a
  dialogue choice, inventory slot, quest objective, or lobby member.
- Step 2's optional data modules may provide state reduction, but they do not
  create UI or prescribe how content looks.
- Reference examples prove that the primitives compose across different game
  types. They are not runtime dependencies and do not become mandatory
  templates or built-in presentation systems.

### Ownership and dependency boundaries

- `TextLayoutEngine` owns immutable paragraph layout results: grapheme
  boundaries, shaping runs, line breaks, glyph positions, alignment,
  truncation, selection geometry, and hit testing. Renderers consume
  positioned glyphs and do not implement wrapping.
- `FontResolver` owns primary/fallback font selection and missing-glyph
  diagnostics. `FontAtlas2D` owns GPU atlas pages and rasterized glyph lifetime
  only.
- `RichTextParser` is a pure validated parser producing text and styled spans.
  It does not perform layout, execute callbacks, or load textures directly.
- `UiMutationQueue` owns lifecycle-safe create, clone, remove, and reparent
  operations and applies them between event dispatch and layout.
- `UiTweenSystem` owns time-based presentation values. It addresses nodes by
  generation-checked handles and cancels work on removal or scene unload.
- Collection virtualization owns visible-range calculation and node recycling.
  It consumes arbitrary rows supplied by game code and knows nothing about the
  row's gameplay meaning.
- Authored HUD files and runtime-created nodes pass through the same schema,
  validation, style, layout, event, clipping, and accessibility paths.

Before choosing text dependencies, write an architecture decision record that
compares HarfBuzz plus a Unicode segmentation/line-break library against the
required Linux/Android footprint. Do not hand-write Unicode shaping, bidi, or
grapheme algorithms. The selected implementation stays behind engine-owned
text contracts.

### Proposed UI and text contracts

```lua
local handle, error = Hud.create("item_rows", {
  id = "row_" .. item.id,
  type = "button",
  text = item.name,
  action = "inspect:" .. item.id,
  style = "list_row"
})

Hud.reparent(handle, "filtered_rows")
Hud.remove(handle)
Hud.clear_children("item_rows")
Hud.set_locale("de-DE")

local count = Text.grapheme_count(label)
local prefix = Text.grapheme_slice(label, 1, visible_count)
```

The example uses an item row only to demonstrate arbitrary runtime content.
The same API must work unchanged for save slots, players, dialogue choices,
debug tools, settings, or any project-defined node.

Handles contain an ID plus generation so a stale callback cannot mutate a new
node that later reuses the same ID. Structural mutations requested during an
event callback are queued and become visible at one documented lifecycle
boundary.

Rich text uses a small allowlist such as `[color]`, `[em]`, `[strong]`,
`[icon]`, and `[link]`. Unknown or malformed tags are diagnostics or literal
text according to one documented strictness setting. Content cannot name Lua
functions or renderer resources outside validated asset references.

### Deliverables

1. Add text wrapping, horizontal and vertical alignment, truncation, overflow,
   line spacing, selection geometry, and scroll-to-reveal behavior.
2. Add Unicode-safe text iteration, bidi/shaping boundaries, fallback fonts,
   and IME composition without exposing the font backend to game code.
3. Add validated rich spans for color, emphasis, links, and inline icons while
   keeping arbitrary executable markup out of content files.
4. Add runtime UI node create, clone, remove, reparent, lookup, and traversal
   through the same path used by HUD documents.
5. Add project-authored reusable UI prefabs with validated parameters and
   explicit instance ownership. Do not ship genre-specific engine prefabs.
6. Add data-driven list and grid population with bounded node virtualization,
   stable row keys, deterministic ordering, and focus retention.
7. Add typed UI events for value change, focus, submit, cancel, pointer
   enter/exit, press/release, drag/drop, and scrolling.
8. Add UI tweens for opacity, position, scale, and color with cancellation tied
   to node lifetime and a reduced-motion policy.
9. Add runtime locale switching, localization expansion diagnostics,
   pseudo-localization, and layout invalidation when language or font changes.
10. Add reusable scrolling, clipping, keyboard/controller navigation, touch
    interaction, and accessibility metadata without assuming a screen layout.

### Implementation slices

#### 3A. Text measurement and wrapping

1. Separate font lookup/rasterization from paragraph measurement.
2. Introduce immutable `TextLayoutRequest` and `TextLayoutResult` values so
   headless tests do not require a GPU.
3. Implement explicit newline, word/grapheme wrapping, unbreakable-token
   fallback, horizontal/vertical alignment, max lines, ellipsis, selection
   ranges, and scroll extents.
4. Cache by text, spans, style, font revision, width, locale, direction, and
   scale; bound the cache and expose hit/miss/memory statistics.
5. Feed HUD text and world text through compatible shaping and measurement
   rules, with their rendering projections remaining separate.

#### 3B. Unicode, input, and rich spans

1. Decode and validate UTF-8 once at the text boundary.
2. Segment grapheme clusters, resolve bidi runs, and shape script runs through
   the selected backend adapter.
3. Resolve fallback fonts per run and preserve stable atlas ownership during
   reload.
4. Parse rich spans into text plus style, link, and icon metadata; layout icons
   as glyph-like boxes with documented baseline rules.
5. Expose caret movement, selection, composition ranges, and link hit results
   as backend-neutral values and typed UI actions.

#### 3C. Safe dynamic retained UI

1. Extract one node validator and constructor used by both `UiDocument` loading
   and runtime creation.
2. Queue structural mutations during action dispatch and apply them before the
   next layout pass.
3. Define focus restoration when the focused node is hidden, disabled,
   removed, reparented, or recycled.
4. Emit capture-cancel and drag-cancel events when a captured node or one of its
   ancestors is removed.
5. Use generation-checked handles for mutation, tweening, focus, capture, and
   asynchronous completion callbacks.
6. Make failed multi-node creation or prefab instantiation transactional so it
   leaves the previous UI tree intact.

#### 3D. Reusable composition and collection virtualization

1. Define project-authored UI prefab files as parameterized `UiNode` trees with
   stable internal IDs and explicit instance roots.
2. Separate collection data, stable row keys, visible-range calculation, and
   row-node binding so game code may replace any one of them.
3. Recycle only nodes outside the overscan range and reset focus, capture,
   transient style, subscriptions, and tweens before rebinding them.
4. Provide small reference probes for a localized settings form, a virtualized
   data list, a changing lobby roster, and mixed-script text. These probes use
   public APIs and share no hidden engine implementation.
5. Exercise mouse, keyboard, controller, and Android touch through the same
   actions and focus model.

### Failure and edge-case matrix

- CJK text without spaces, right-to-left text, mixed-direction text, combining
  marks, emoji sequences, invalid UTF-8, and fonts changing during layout;
- one token wider than its box, zero/negative available size, huge font scale,
  missing glyphs, missing fallback fonts, and atlas exhaustion;
- malformed or nested rich tags, missing inline icons, overlapping links, and
  localization values containing markup characters;
- locale, DPI, safe area, orientation, or window size changing while text is
  selected, edited, scrolled, or being animated;
- a node removes itself, its parent, or a captured sibling during an event;
- create/remove/reparent requests targeting the same node in one dispatch;
- duplicate runtime IDs, stale handles, failed prefab parameters, prefab ID
  collisions, and partial multi-node construction failure;
- a virtualized row disappearing, moving, or changing height while focused,
  captured, dragged, or inside the overscan range;
- repeated list recycling without leaking callbacks, tweens, subscriptions,
  accessibility nodes, or atlas references;
- reduced-motion mode enabled during an active tween and scene unload during a
  tween completion callback;
- IME composition interrupted by focus loss, node removal, locale change, or
  application suspension;
- an invalid watched HUD, theme, localization, font, icon, or prefab edit must
  preserve the last valid live UI.

### Test and performance gates

- golden headless layout positions for Latin, CJK, RTL, combining marks, emoji,
  and pseudo-localized strings at 16:9, 4:3, ultrawide, and portrait sizes;
- fuzz/property tests for markup parsing, UTF-8 boundaries, grapheme slicing,
  bidi run construction, and hostile nesting depth;
- mutation-ordering tests for removal, creation, reparenting, capture, focus,
  and recursive callback requests;
- prefab validation and transactional-instantiation tests for missing
  parameters, duplicate IDs, nested instances, and failed child creation;
- virtual-list tests for stable ordering, variable row heights, focus
  restoration, recycling reset, and live collection mutation;
- bounded layout-cache, atlas, tween, subscription, and recycled-node memory
  tests across repeated scene and locale changes;
- performance budgets for ten thousand logical collection rows with only the
  visible range plus bounded overscan represented by live nodes;
- Linux and Android reference probes must produce equivalent layout decisions
  for the same logical canvas, locale, font set, and scale.

### Done when

- A developer can construct and update arbitrary retained UI trees at runtime
  without changing C++ or bypassing validation.
- Long translated text wraps, shapes, selects, and scrolls correctly at desktop
  and phone aspect ratios without manually inserted line breaks.
- Inventory, quest log, lobby, settings, save browser, dialogue, and editor
  screens can generate their content without predeclaring every possible node.
- A visual novel can be authored from the general text, data, UI, audio, input,
  and save APIs, but no dedicated visual-novel UI or presentation policy exists
  in the engine.
- UI teardown cannot leave active tweens, pointer captures, focus, callbacks,
  subscriptions, accessibility nodes, or recycled content bindings.

## Step 4 — Reusable 2D and Isometric Game Kits

The engine primitives are stable; developers now need small, replaceable Lua
packages that compose them into common game workflows.

**Status: planned.** Several examples already contain useful modular Lua
controllers, but there is no versioned package contract, consistent event
vocabulary, or reusable test harness for composing them in a new project.

### Package architecture

Packages live outside `src/demi` and depend only on installed public Lua APIs.
Each package contains:

```text
package.json
scripts/<package modules>.lua
schemas/<optional data schemas>.json
prefabs/<optional composition>.prefab.json
tests/<headless scenarios>.lua
README.md
```

The manifest declares package ID/version, engine capability requirements,
Lua modules, asset/prefab/data dependencies, exported events, and configurable
defaults. A package cannot read another package's private module path; declared
dependencies expose explicit public modules.

In this step, packages are checked into the engine distribution or directly
vendored into a project. Step 7 later adds installation, dependency solving,
and lock files without changing these Lua/event/data contracts.

Avoid a `GameplayManager` or universal character component. Packages exchange
small values and events while the game owns composition and policy.

### Shared event vocabulary

Define JSON/Lua-shaped payload contracts before packages depend on them:

```lua
Events.emit("damage_requested", {
  source = projectile.owner,
  target = hit.entity_id,
  amount = projectile.damage,
  type = "physical",
  point = hit.point,
  normal = hit.normal,
  tags = {"projectile"}
})
```

The damage package validates and resolves this into `damage_applied`,
`health_changed`, and possibly `entity_defeated`. It does not award score,
choose animations, or destroy the entity unless configured by the game. Event
payload schemas are testable project data.

### Deliverables

1. Package the existing platform, top-down, click-to-move, and isometric
   controllers under a documented project package layout.
2. Add optional packages for:
   - health, damage, teams, invulnerability, and death events;
   - projectile and hit-scan weapons using pooling and physics queries;
   - pickups, checkpoints, respawning, and scene entrances;
   - camera zones, shake, look-ahead, and room transitions;
   - inventory/equipment and interaction prompts;
   - wave spawning, objectives, and encounter completion.
3. Define small event contracts between packages rather than one large game
   framework or global state singleton.
4. Keep tuning data in validated `DataAsset` files and entity composition in
   prefabs.
5. Provide override points for movement, targeting, damage, and progression
   policy without copying package internals.
6. Add headless scenario tests for high-speed projectiles, repeated pooling,
   simultaneous contacts, scene transitions, destroyed targets, and save/load.
7. Update the platformer, shooter, fighting, and isometric examples to consume
   the packages where that makes them clearer.

### Implementation slices

#### 4A. Package format and test harness

1. Define deterministic in-project locations, public module names, direct
   dependency declarations, engine capability requirements, and conflict
   diagnostics. Defer external package resolution and lock files to Step 7.
2. Extend `demi script check` and project validation to include installed
   package modules and their declared schemas.
3. Add a lightweight headless package test runner with isolated world, events,
   deterministic time/random, and fixture-prefab support.
4. Generate LuaLS paths/types from installed package manifests.

#### 4B. Health, damage, and interactions

1. Implement health as explicit gameplay state associated with stable entity
   IDs, with clamp/max/invulnerability rules configured per instance.
2. Keep teams/friendly-fire, resistances, critical hits, score, loot, and death
   policy as optional strategies/data rather than required engine behavior.
3. Define removal behavior when a target is destroyed during damage dispatch.
4. Add interaction queries/prompts that select candidates deterministically by
   priority, distance, and stable ID.

#### 4C. Projectiles and weapons

1. Separate weapon cooldown/ammo policy, projectile motion, hit detection, and
   damage request emission.
2. Use CCD/raycast or shape cast according to declared projectile mode; never
   rely only on final-frame overlap for fast shots.
3. Reset every mutable field when releasing/claiming pooled projectiles.
4. Make ownership, ignored entity, collision mask, lifetime, pierce count, and
   hit-once tracking explicit.

#### 4D. Traversal, checkpoints, and cameras

1. Adapt existing public controllers instead of duplicating physics loops.
2. Define checkpoint state by stable entrance/checkpoint IDs and serializable
   respawn data.
3. Keep scene transitions deferred and use scene preparation for room changes.
4. Compose camera follow, bounds, zones, shake, and look-ahead as independent
   policies writing to the existing camera service.

#### 4E. Isometric encounters

1. Extract generic wave/objective scheduling from the tower-defense example
   without moving tower targeting or economy rules into the engine.
2. Use existing grid occupancy/navigation and stable prefab IDs for units.
3. Define deterministic spawn failure, path unavailable, objective failed,
   and encounter complete events.

### Failure and edge-case matrix

- source and target destroyed during event dispatch;
- self-damage, friendly fire, simultaneous lethal hits, healing above maximum,
  zero/negative damage, and invulnerability ending on the same frame;
- a projectile starts inside a collider, crosses multiple colliders, strikes a
  trigger before a solid, or is released twice;
- pooled objects retain contacts, timers, callbacks, owner IDs, visual state,
  or network registration;
- checkpoint belongs to an unloaded scene or its prefab version changed;
- camera zones overlap, disappear, or change priority during transition;
- interaction candidates tie exactly or are disabled before confirmation;
- an isometric unit blocks the path during a wave calculation;
- save/load occurs with active projectiles, queued damage, or an in-progress
  scene transition;
- incompatible declared direct dependency and two packages exporting the same
  public module/event schema.

### Reference and test matrix

| Probe | Required package behavior |
|---|---|
| Platformer | controller, hazards, health, pickups, checkpoints, camera zones |
| Top-down shooter | aim, pooled projectiles/hitscan, teams, damage, interactions |
| Fighting game | health/events only; fighter timing remains its specialized package |
| Isometric builder | objectives/waves plus existing placement/navigation; economy remains game policy |

Each probe gets replay tests for ordinary input and adversarial lifecycle
fixtures. Package tests must also run without rendering to prevent presentation
from becoming an implicit dependency of gameplay policy.

### Done when

- A developer can assemble a small platformer, top-down action game, puzzle,
  or tower-defense prototype without adding engine bindings.
- Packages can be used independently and replaced without changing engine
  source.
- Common edge cases are covered outside individual example scripts.

## Step 5 — Lightweight 3D Creation Workflow

Lightweight 3D should be approachable without understanding renderer or Jolt
internals.

**Status: planned.** Runtime rendering and physics are usable, but diagnosing
third-party model orientation, scale, material, animation, collider, lighting,
and cost still requires too much engine knowledge and visual trial-and-error.

### Ownership and data flow

```text
source model
  -> model importer and explicit import profile
  -> normalized engine-space model asset
  -> model inspection/collider recommendation reports
  -> prefab composition
  -> renderer and PhysicsWorld3D consume the same normalized transforms
```

- `ModelImportProfile` owns author choices such as source up/forward axis,
  meters-per-unit, root-node treatment, animation selection, material policy,
  and mesh optimization. Conversion is performed during import, not patched in
  examples or renderers.
- `ModelInspector` is a read-only asset tool returning structured data. Text
  and future editor views format the same report.
- `ColliderRecommendation` analyzes geometry and body intent but never silently
  changes authored physics. The developer accepts a generated collider asset
  or authors another collider.
- Runtime render extraction and Jolt integration consume engine-space
  transforms and explicit components. Neither guesses source-format axes.

### Proposed CLI contracts

```text
demi asset inspect model.asset.json --section nodes,materials,animations,bounds
demi asset inspect model.asset.json --format json
demi asset collider model.asset.json --recommend --body static
demi asset collider model.asset.json --detail 0.75 --preview collider.scene.json
demi validate demi.project.json
```

The JSON inspection form is stable enough for editor consumption but may add
fields compatibly. Diagnostics identify source node/primitive/material names
and the normalized engine-space result.

### Deliverables

1. Add CLI inspection for model nodes, meshes, materials, textures, skeletons,
   clips, bounds, scale, axis orientation, and generated collider previews.
2. Add import presets for static props, animated characters, environment
   meshes, and billboards with explicit unit and coordinate conversion.
3. Add automatic collider recommendations and validation for static triangle
   meshes, dynamic convex shapes, triggers, and character controllers.
4. Add prefab templates for a first-person controller, third-person
   controller, moving platform, pickup, projectile, door, and camera rig.
5. Add lighting and environment presets that remain ordinary material, light,
   camera, and post-effect data.
6. Add material/debug views for normals, UVs, alpha, lighting, bounds,
   colliders, overdraw, and instancing eligibility.
7. Add documented mobile budgets and automatic diagnostics for excessive
   lights, shadow passes, transparent draws, unique meshes, and texture memory.
8. Create a small exploration reference that imports a third-party glTF,
   traverses moving platforms, activates triggers, collects items, and runs on
   Linux and Android without engine edits.

### Implementation slices

#### 5A. Import profiles and inspection

1. Add versioned import-profile fields to model manifest settings and schema.
2. Normalize node transforms, skin bind poses, animation channels, bounds,
   normals/tangents, and collider source geometry through one conversion
   matrix.
3. Report non-uniform/negative scale, missing normals/tangents/UVs, duplicate
   material names, unsupported primitive modes, excessive joints/influences,
   and external URI dependencies.
4. Preserve original source names as inspection metadata while runtime IDs
   remain deterministic.

#### 5B. Collider workflow

1. Extend the existing collider generator with recommendation output based on
   body type, vertex/triangle count, concavity, and animation/skinning.
2. Make unsafe combinations validation errors: dynamic triangle mesh,
   non-positive dimensions, unsupported scaled mesh, or animated vertices used
   as an implicit static collider.
3. Store generation inputs and source hash in collider manifests so stale
   colliders are detected.
4. Render preview/debug geometry from the exact collider data supplied to
   Jolt.

#### 5C. Prefabs and presentation presets

1. Build small prefab compositions from public components; controller behavior
   remains in reusable Lua modules.
2. Define environment/camera/light presets as ordinary prefabs/materials, not
   special renderer modes.
3. Include clear extension points for game-specific movement and animation
   state mapping.
4. Validate every prefab against Android-supported material, texture, light,
   and particle budgets.

#### 5D. Diagnostic render modes

1. Add renderer-neutral debug-mode requests to the camera/render frame.
2. Implement normals, UV checker, alpha/cutoff, lighting-only, bounds,
   collider, overdraw approximation, and batch/instance grouping views.
3. Keep debug shaders cooked through the same shader pipeline and excluded
   from release packages unless requested.

### Failure and edge-case matrix

- empty scene, multiple roots, deeply nested nodes, sparse accessors, embedded
  buffers/images, external files, and malformed bounds;
- Z-up, X-forward, centimeters, mirrored transforms, negative/non-uniform
  scale, and animation on transformed parent nodes;
- missing texture, opaque texture with requested cutoff, mismatched color
  space, UV outside range, and material without a supported shader fallback;
- skin with missing inverse bind matrices, more influences than supported,
  duplicate clip names, zero-duration clip, and incompatible skeletons;
- collider detail at 0, 1, NaN/out-of-range input, degenerate triangles,
  disconnected geometry, and source changes after generation;
- moving platform rotated/scaled while occupied, character spawning inside a
  collider, tunneling projectile, trigger and solid overlap in one step;
- device loss/surface recreation and asset unload while a model instance or
  animation palette still references GPU data;
- Noop/headless inspection must not require creating a graphics device.

### Test and budget gates

- checked glTF/GLB fixtures for every transform/material/skin edge case;
- golden normalized transforms, bounds, animation poses, and collider data;
- render extraction tests proving primitive type and object scale are retained;
- Jolt/debug-shape agreement tests and replayed controller/platform scenarios;
- Noop renderer ownership tests across reload/unload/failure;
- reference budgets for visible instances, unique meshes, triangles, texture
  residency, lights, shadow passes, draw submissions, and CPU/GPU frame time;
- an Android package/device smoke covering model textures, alpha, animation,
  lighting, physics, suspend/resume, and surface recreation.

### Done when

- Import orientation, scale, materials, animation, and colliders can be
  diagnosed without modifying renderer code.
- The reference scene meets declared Linux and Android frame/memory budgets.
- Gameplay scripts depend only on engine-facing transforms, physics, cameras,
  animation, audio, and prefab APIs.

## Step 6 — Multiplayer Game Workflow

Networking should provide the lifecycle and debugging developers repeat while
leaving game authority and rules explicit.

**Status: planned.** `NetworkSession` already isolates transport, identity,
authority, snapshots, and events. Reference games still own conventions for
RPC validation, late join, roster repair, replicated prefab selection,
prediction, and lobby flow.

### Layer boundaries

```text
Lua game policy and optional lobby/gameplay packages
  -> NetworkSession game-facing contract
    -> replication/RPC/session services
      -> transport and security adapters (ENet/TLS/DTLS)
```

- `NetworkSession` owns connection/session state, peers, stable network IDs,
  authority, and diagnostics. It does not know health, weapons, teams, or score
  rules.
- `ReplicationRegistry` consumes component metadata and prefab declarations to
  encode only explicitly allowed fields.
- `RpcRegistry` owns declarations, payload validation, authority/rate rules,
  and dispatch. Transport messages cannot directly name arbitrary Lua
  functions.
- `NetworkSimulation` is a test/development adapter between session and
  transport; it applies deterministic delay/loss/reorder using a recorded seed.
- Prediction/reconciliation helpers are opt-in policies above fixed simulation
  and do not change authoritative world ownership.

### Proposed authored contract

```json
{
  "format_version": 1,
  "replicated_prefabs": {
    "player": {
      "prefab": "prefab://network/player",
      "fields": {
        "Transform2D": {
          "position": {"rate": 20, "reliability": "unreliable"},
          "rotation": {"rate": 10, "reliability": "unreliable"}
        },
        "GameplayData": {
          "values.health": {"rate": 5, "reliability": "reliable"}
        }
      }
    }
  },
  "rpcs": {
    "fire": {
      "direction": "client_to_authority",
      "reliability": "unreliable",
      "rate_limit": 20,
      "schema": "asset://schemas/rpc/fire"
    }
  }
}
```

Metadata must still permit replication for every selected component field;
project data can narrow that permission but cannot widen it.

### Deliverables

1. Add declarative replicated prefab fields driven by component metadata,
   including change detection, rates, reliability, and owner visibility.
2. Add typed game events/RPC declarations with sender, target, authority,
   payload validation, size limits, and rate limits.
3. Add automatic late-join spawn and current-state replay for active replicated
   entities and session values.
4. Add explicit ownership transfer, reconnect, despawn, peer-disconnect, and
   session-reset lifecycles.
5. Add lobby metadata, ready state, capacity, teams, and coordinated scene
   activation as optional Lua packages above `NetworkSession`.
6. Add host input queues, snapshots, interpolation, prediction, and
   reconciliation helpers with documented suitability and limitations.
7. Add latency, jitter, loss, duplication, reordering, and disconnect
   simulation to headless tests and profiler diagnostics.
8. Add a one-command local multi-process test and dedicated-server packaging
   template.
9. Update the FFA shooter so it does not manually rebuild roster, late-join,
   or generic replicated-prefab behavior.

### Implementation slices

#### 6A. Declared replication

1. Resolve declared prefabs/fields during project validation and assign stable
   schema hashes for handshake compatibility.
2. Track per-peer acknowledged baselines and field changes without serializing
   the entire world every tick.
3. Separate spawn/despawn reliability from high-rate state updates.
4. Retain enough authoritative current state for deterministic late-join
   reconstruction within explicit memory limits.

#### 6B. RPC and event safety

1. Validate payload size/depth/type before creating Lua values.
2. Apply sender direction, authority, target, session-state, and rate rules
   before dispatch.
3. Queue RPC callbacks at a simulation synchronization point; transport threads
   never enter Lua or mutate the world.
4. Return rejection diagnostics/counters without reflecting sensitive details
   to an untrusted peer.

#### 6C. Lifecycle and lobby packages

1. Define peer connected/disconnected/reconnected events with stable ID and
   reason.
2. Specify ownership transfer/despawn policy for voluntary disconnect,
   timeout, host shutdown, and scene transition.
3. Implement ready/team/map/session metadata as an optional Lua package using
   replicated session values.
4. Coordinate `Scene.prepare` and activation after every required peer reaches
   the declared readiness barrier, with timeout/host policy in game code.

#### 6D. Action-game helpers

1. Timestamp and sequence client input commands against fixed ticks.
2. Keep an authoritative input queue with duplicate/old/future rejection.
3. Add snapshot interpolation buffers with bounded extrapolation.
4. Add opt-in local prediction and reconciliation callbacks that operate on
   explicit serializable controller state.
5. Document that physics rewind/lag compensation is limited and implement it
   only for bounded query snapshots, never by mutating the live world mid-step.

#### 6E. Test and deployment workflow

1. Start server and clients on dynamically allocated ports from one test
   command and capture each structured log separately.
2. Add deterministic network simulation profiles committed as fixtures.
3. Package a headless server without renderer/UI assets unless reachable by
   server-side project dependencies.

### Failure and security matrix

- protocol/schema/capability mismatch during handshake;
- duplicate network ID, unknown prefab, unauthorized field, malformed payload,
  excessive nesting, oversized string/array, NaN, and replayed RPC sequence;
- late join while entities spawn/despawn or the active scene changes;
- disconnect during ownership transfer, reliable RPC delivery, or snapshot
  baseline update;
- host migration is explicitly unsupported until designed; it must fail
  clearly rather than electing implicitly;
- packet loss/reorder/duplication around spawn-before-state and
  despawn-before-late-state ordering;
- a local predicted entity is destroyed or authority changes during
  reconciliation;
- reconnect with stale session token, network ID, or prefab revision;
- malicious rate-limit exhaustion and invalid-message log flooding;
- Android suspend/background transition and network-interface change;
- repeated connect/reset/disconnect under ASan/LSan with no retained peers,
  callbacks, entities, or buffers.

### Test matrix

- codec property/fuzz tests independent of sockets;
- authority and rate-rule unit tests with a fake clock;
- deterministic simulated-transport tests for every failure profile;
- two- and four-client process tests covering late join and disconnect;
- dedicated-server tests proving no graphics/window initialization;
- replay comparison of authoritative state after identical command streams;
- bandwidth, buffer-size, rejected-message, and reconciliation counters with
  enforceable budgets.

### Done when

- A developer can build a lobby plus a small host-authoritative match using
  public APIs and project packages.
- Late join, reconnect, ownership transfer, and disconnect pass repeatable
  multi-client tests.
- Network failures identify the peer, message/RPC, authority rule, and payload
  involved.

## Step 7 — Asset Iteration, Streaming, and Project Packages

Larger games need fast iteration and explicit memory ownership rather than a
single startup asset set.

**Status: planned.** Manifests, dependency validation, cooking, portable asset
packages, scene resource groups, and startup upload are present. Importer
registration, incremental dependency cooking, addressable groups, and reusable
project-package version resolution are not complete developer workflows.

### Ownership model

- `AssetImporterRegistry` owns importer descriptors and construction. An
  importer reads source plus settings and returns generated outputs,
  dependencies, metadata, and diagnostics; it does not upload runtime
  resources.
- `AssetCookGraph` owns reachable dependency calculation and cache keys. It is
  deterministic and independent of timestamps.
- `AssetGroupService` owns asynchronous acquire/release state and progress.
  Renderer/audio/script backends own their resource objects behind narrow
  loaders; the group service owns references, not backend internals.
- `ProjectPackageResolver` owns package manifests, versions, dependency
  solving, lock data, and stable-ID collision policy. It never silently renames
  assets/components/modules.
- Hot reload and normal load use the same asset handlers and resource lifetime
  registry. There is no separate editor-only cache owner.

### Asset-group contract

```json
{
  "format_version": 1,
  "id": "asset-group://chapter_02",
  "roots": [
    "scene://chapter_02",
    "asset://voice/chapter_02",
    "asset://backgrounds/chapter_02"
  ],
  "budget": {
    "resident_mb": 256,
    "upload_ms_per_frame": 3
  }
}
```

```lua
local request = Assets.prepare_group("asset-group://chapter_02")
local progress = Assets.progress(request)
if Assets.is_ready(request) then
  Assets.activate(request)
end
Assets.cancel(request)
Assets.release_group("asset-group://chapter_01")
```

Progress is monotonic for a request and reports stages (`resolve`, `read`,
`decode`, `upload`, `ready`). Cancellation is cooperative; activation is
explicit and atomic with respect to group ownership.

### Deliverables

1. Add registered importer and asset-handler boundaries with versioned settings
   schemas, dependencies, generated outputs, and platform variants.
2. Add addressable asset groups with asynchronous preload, progress,
   cancellation, activation, reference tracking, unload, and memory reports.
3. Add incremental cooking based on importer version, source hash, settings,
   and transitive dependency hashes.
4. Add texture atlas, sprite metadata, and font atlas generation through normal
   manifests rather than custom example scripts.
5. Add per-platform compression and quality overrides while preserving one
   stable asset ID.
6. Route hot reload through the same resource ownership path as ordinary load
   and unload.
7. Add versioned project packages for reusable Lua modules, prefabs, UI,
   assets, schemas, and optional registered engine extensions.
8. Add dependency/version/conflict diagnostics and deterministic package lock
   data; never silently rewrite a stable ID.

### Implementation slices

#### 7A. Importer registry

1. Define descriptor metadata: supported extensions/types, importer version,
   settings schema, output types, thread-safety, and platform capabilities.
2. Move existing importer dispatch behind the registry without changing
   authored manifests.
3. Reject ambiguous importer selection unless the manifest chooses explicitly.
4. Test third-party/in-tree registration without requiring a central switch.

#### 7B. Incremental cook graph

1. Compute each node key from importer identity/version, normalized settings,
   all source hashes, target platform/profile, and dependency output hashes.
2. Store cache metadata outside source directories and verify every cached
   output before reuse.
3. Rebuild only reverse-reachable dependents after a change.
4. Produce a machine-readable explanation for every cache hit/miss.

#### 7C. Asynchronous groups

1. Separate IO/decode work from main-thread/backend upload work.
2. Bound queues, temporary decoded memory, per-frame upload time, and resident
   memory.
3. Deduplicate shared resources across groups and release only after the final
   scene/persistent/group owner is gone.
4. Make failed/cancelled preparation roll back every acquired dependency and
   temporary resource.
5. Coordinate activation with scene preparation without creating two resource
   ownership systems.

#### 7D. Generated atlases and fonts

1. Treat atlas/font generation as registered importers with deterministic
   manifests and outputs.
2. Emit sprite rectangles, pivots, borders, animation tags, glyph ranges,
   fallback references, and padding/bleed settings as validated metadata.
3. Preserve source asset IDs or provide explicit migration maps when packing
   changes runtime texture layout.

#### 7E. Project packages

1. Define a package manifest and lock format with engine capability/version
   requirements and content hashes.
2. Resolve a dependency graph deterministically and reject cycles or
   incompatible constraints with a minimal conflict explanation.
3. Preview all files, stable IDs, Lua module names, schemas, and extension
   registrations before installation.
4. Install to a package-owned directory and update the lock atomically.
5. Support uninstall only when no authored project reference or dependent
   package remains, unless an explicit breaking removal is requested.

### Failure and edge-case matrix

- importer throws/fails after writing some outputs, changes version, or emits
  an undeclared dependency;
- cache metadata exists but output is missing/corrupt or was built for another
  platform/profile;
- source changes while cooking and two cook processes target the same cache;
- cancelled group during IO, decode, GPU upload, activation, or scene callback;
- two groups share an asset; one reloads or unloads while the other renders;
- resource exceeds budget, upload queue stalls, Android sends low-memory, or
  graphics surface is recreated during upload;
- cyclic asset groups, dependency error midway, duplicate activation, release
  without acquire, and stale request handle;
- atlas edge bleed, rotated/trimmed sprites, nine-slice borders, empty sprites,
  and animation frames spanning pages;
- missing font glyph after packaging and fallback cycle;
- package path traversal, checksum mismatch, duplicate stable ID/module,
  dependency cycle, lock interruption, and uninstall with live references.

### Test and observability plan

- fake importer and fake resource-backend tests for every failure stage;
- deterministic cook graph golden keys and reverse-invalidation tests;
- resource lifetime stress tests shared with scene failure coverage;
- counters for pending bytes, decoded bytes, resident bytes per backend/group,
  upload queue time, cache hit reason, and asset owners;
- Linux and Android cook/package round trips from a clean checkout;
- hot reload versus ordinary load parity tests comparing resulting metadata and
  resource counts.

### Done when

- A loading screen can prepare a content group, show progress, activate it,
  and unload the previous group with memory returning to budget.
- A content-only package can be installed without copying files by hand.
- Incremental cook and reload never duplicate GPU, audio, script, or scene
  resources.

## Step 8 — Functional Editor on Runtime Contracts

The editor should accelerate the same files and commands used by the CLI. It
must not become a second engine with hidden state.

**Status: planned.** `demi editor` and `src/editor` establish a boundary, but
the executable is still a placeholder. Component metadata, prefab expansion,
scene commands, validation, rendering, and debug services now provide the
contracts the editor should consume.

### Editor architecture

- `EditorApplication` coordinates panels, documents, play sessions, and
  commands. It does not parse scene JSON, inspect component internals, or
  mutate runtime storage directly.
- `EditorDocument` owns one authored source document, its last saved canonical
  representation, dirty state, diagnostics, external-file revision, and undo
  stack.
- `EditorCommand` describes a validated source mutation with apply/revert and
  serialization. Commands target stable IDs/field paths rather than pointers.
- Inspector/property widgets are generated from component/UI/asset metadata.
  Specialized editors adapt values but still submit ordinary commands.
- Scene/Game previews run through the existing bgfx hosts and runtime services.
  Editor rendering owns separate views/targets, not another renderer.
- File IO is behind a document store that performs conflict detection and
  atomic saves. Panels never write files directly.

The first implementation should choose a single UI technology behind an
`EditorUiHost` boundary and document that decision. The authored/runtime data
contracts must not depend on Dear ImGui, another immediate UI library, or any
specific docking implementation.

### Command contract

```cpp
struct EditorCommandResult {
  bool applied = false;
  Diagnostics diagnostics;
};

class EditorCommand {
public:
  virtual ~EditorCommand() = default;
  virtual EditorCommandResult apply(EditorDocument &) = 0;
  virtual EditorCommandResult revert(EditorDocument &) = 0;
  virtual Json serialize() const = 0;
};
```

This is a design shape, not a requirement for one inheritance hierarchy.
Commands may be value variants dispatched by a command service if that is
clearer. The required properties are explicit target, validation, reversible
state, deterministic serialization, and no hidden live pointers.

### Deliverables

1. Add project creation/template selection and asset import status.
2. Add hierarchy editing with create, rename, parent, enable, duplicate, and
   delete operations.
3. Generate the inspector from component and UI metadata with inline shared
   validation diagnostics.
4. Add 2D and 3D scene views, selection, transforms, snapping, collider
   visualization, camera previews, and play-from-here.
5. Add prefab create/open/apply/revert/override workflows.
6. Add UI hierarchy, anchors/layout editing, safe-area presets, localization
   preview, and dynamic-data sample preview.
7. Add focused animation, material, dialogue-data, input-action, and audio
   inspectors without introducing editor-only formats.
8. Implement every mutation as a serializable undoable command.
9. Add Play/Pause/Step/Stop with an explicit edit/play state boundary.
10. Integrate diagnostics, logs, profiler, network simulation, input state,
    and render/physics debug views.

### Implementation slices

#### 8A. Documents and commands

1. Open project/scene/prefab/HUD/material/data documents through existing
   parsers and retain source-format version.
2. Implement commands for entity/node create, delete, rename, enable, parent,
   duplicate, component add/remove, and field set.
3. Validate a command against a staged document before committing it to the
   undo stack.
4. Group continuous gizmo/slider edits into one transaction while still
   previewing intermediate values.
5. Detect external changes and offer reload, keep, or structural merge only
   when a safe merge contract exists.

#### 8B. Generated hierarchy and inspector

1. Build hierarchy rows from stable entity/UI IDs and resolved parent links.
2. Generate field editors from descriptor type, bounds, enum, asset/entity
   reference kind, read-only/restart policy, and documentation.
3. Use the same diagnostics and reference resolver as `demi validate`.
4. Keep multi-selection editing explicit: show mixed values and submit one
   atomic multi-target command.

#### 8C. Scene and game views

1. Allocate editor camera views/render targets through normal GPU resource
   owners.
2. Implement ID-buffer or deterministic CPU picking without embedding editor
   IDs into authored components.
3. Express gizmo operations in local/world transform APIs with snapping and
   parent-aware conversion.
4. Show colliders, bounds, lights, cameras, navigation, UI safe areas, and
   renderer diagnostic modes using runtime debug extraction.

#### 8D. Prefab and UI workflows

1. Display resolved prefab source, instance overrides, nested instance IDs,
   and invalid/missing references.
2. Apply/revert overrides as source commands with a preview diff.
3. Reuse runtime layout for UI preview at selectable viewport/DPI/safe-area/
   locale settings.
4. Allow sample data to populate dynamic lists without saving sample runtime
   nodes into authored HUD data.

#### 8E. Play mode

1. Snapshot the validated authored world and start a separate runtime world.
2. Route input/focus deliberately between editor and game views.
3. Pause, fixed-step, inspect runtime values read-only, and stop with complete
   scene/script/resource teardown.
4. Do not copy runtime mutations back automatically. A future explicit
   “apply selected runtime value” command must validate and show its target.

### Failure and edge-case matrix

- undo after target deletion/rename, nested transaction failure, command
  validation rejection, and undo stack across document reload;
- duplicate stable IDs, prefab cycles, missing asset references, and a field
  becoming invalid after another command;
- external edit while dirty, file deleted/renamed, save permission failure,
  storage full, and crash during atomic save;
- multi-selection with incompatible component sets;
- selection/picking after entity destruction or scene reload;
- negative/non-uniform parent scale during world-space gizmo movement;
- renderer/device failure while game view is active;
- play-mode script exception, scene transition, network session, hot reload,
  and stop during callback/command processing;
- prefab apply affecting several open scenes and undo after dependency reload;
- locale/font/safe-area change while editing UI;
- editor closure with dirty documents and running background asset work.

### Test strategy

- command unit tests use in-memory documents and shared validators;
- property tests assert `apply -> revert` restores canonical source exactly;
- golden serialization tests ensure deterministic formatting;
- panel/view models are tested without a GPU/UI toolkit;
- Noop-backed scene/game-view lifetime tests cover repeated open/play/stop;
- scripted end-to-end tests create a project, edit a scene/prefab/UI, undo,
  save, validate through CLI, run, and compare expanded output;
- crash-recovery tests never treat autosaved/editor cache state as authored
  source without explicit user recovery.

### Done when

- An editor-authored project produces the same validated source and expanded
  scene as an equivalent CLI-authored project.
- Every edit supports undo/redo and deterministic formatting.
- A small game can be assembled, tested, diagnosed, and packaged without
  manually editing JSON, while direct text editing remains supported.

## Step 9 — Shipping Linux and Android Games

Packaging should cover the ordinary work between a successful debug run and a
release artifact.

**Status: planned.** Linux bundles and Android debug APK staging exist, but
release metadata, signing, permissions, device coverage, and lifecycle
qualification are not yet one compatibility-protected shipping workflow.

### Ownership boundaries

- `ProjectBuildSettings` is versioned project data parsed and validated by the
  same project loader used by CLI/runtime. It contains intent, not Gradle,
  desktop-file, or shell fragments.
- Platform packagers translate validated build settings into generated staging
  trees. Generated Android/Linux metadata is never hand-authored source.
- `PlatformCapabilities` declares what a configured toolchain/runtime supports
  and is consumed by validation, doctor, packaging, and future editor views.
- Signing secrets are external inputs referenced by environment/CI secret
  names. They are never copied into the project, logs, cook manifest, or cache.
- Runtime permission/lifecycle APIs stay behind `ApplicationServices`; game Lua
  does not call JNI or SDL directly.

### Proposed project settings

```json
{
  "build": {
    "application_id": "com.example.mystory",
    "display_name": "My Story",
    "version_name": "1.0.0",
    "version_code": 1,
    "icon": "asset://branding/icon",
    "splash": "asset://branding/splash",
    "window": {"width": 1280, "height": 720, "mode": "windowed"},
    "android": {
      "orientation": "landscape_sensor",
      "min_sdk": 26,
      "permissions": []
    }
  }
}
```

Unsupported/unknown settings and incompatible combinations are validation
errors before Gradle or packaging starts.

### Deliverables

1. Add validated project settings for application ID, title, executable name,
   version, icons, splash screen, window defaults, orientation, and declared
   permissions.
2. Add reproducible Linux release bundles with desktop metadata and correct
   user-data/cache paths.
3. Add Android debug and release variants, ABI selection, SDK policy,
   keystore/signing inputs, APK/AAB output, and generated Gradle staging.
4. Add runtime permission request/status APIs for declared permissions.
5. Complete Android surface recreation, audio focus, suspend/resume, safe area,
   IME, storage, back navigation, and low-memory handling.
6. Add build-time capability validation before packaging begins.
7. Add automated device/emulator smoke tests for scenes, textures, fonts,
   touch, audio, saves, networking, orientation, and lifecycle restoration.
8. Add structured startup/crash diagnostics suitable for packaged builds.

### Implementation slices

#### 9A. Build settings and capability checks

1. Add schema, parser, canonical serializer, migration tests, and CLI inspect
   output for build settings.
2. Validate reverse-DNS application IDs, versions, asset dimensions/formats,
   orientation, SDK ranges, ABI list, permissions, and platform-specific
   feature use.
3. Cross-check reachable project assets/features against configured media,
   network, SVG, graphics API, and Android capability support.

#### 9B. Linux release bundle

1. Produce runtime executable, reachable cooked assets, shared-library policy,
   desktop entry, icons, license/attribution report, and launch script only
   where required.
2. Use XDG-compliant writable/config/cache paths and never write beside the
   executable in a read-only install.
3. Verify the bundle in a clean environment rather than through build-tree
   dependencies.

#### 9C. Android release pipeline

1. Generate Gradle project metadata deterministically from build settings.
2. Support selected ABIs and record native/content hashes in the package
   report.
3. Accept signing configuration through named external inputs and redact all
   command/output diagnostics.
4. Produce aligned/signed APK and AAB variants with reproducible non-secret
   staging content.
5. Record target/min SDK, engine version, capabilities, ABIs, graphics shader
   variants, and source/cook hashes in a build report.

#### 9D. Runtime lifecycle and permissions

1. Model permission states as `unknown`, `not_requested`, `requesting`,
   `granted`, `denied`, and `denied_permanently` with asynchronous result
   events.
2. Request only permissions declared in project settings and expose a useful
   diagnostic when the manifest omitted one.
3. Preserve scene/resource ownership across focus loss and ordinary pause;
   rebuild surface-dependent GPU resources after surface loss without
   duplicating scene/script/audio state.
4. Define audio focus, background networking, save flush, low-memory release,
   IME, safe-area, orientation, and back-action policies explicitly.

#### 9E. Device qualification

1. Create one deterministic automated smoke scene exercising text/fallback
   font, transparent texture, shader/material, audio, touch/IME, save path,
   network loopback, rotation/safe area, suspend/resume, and surface recreation.
2. Collect structured results and screenshots/hashes where pixel-stable output
   is realistic; otherwise assert render/resource counters and visible probe
   IDs.
3. Run at least one emulator/API level in CI and maintain a documented small
   physical-device matrix for GPU/vendor differences.

### Failure and edge-case matrix

- invalid application ID/version, missing branding asset, unsupported image
  size/format, permission used but undeclared, and requested SDK unavailable;
- release build accidentally includes source scripts, debug shaders, profiler
  endpoints, editor data, or unreachable assets;
- path containing spaces/non-ASCII, read-only install, missing system library,
  and stale build-tree dependency;
- missing/wrong signing input, expired certificate, secret printed by a failed
  subprocess, and interrupted package signing;
- install/upgrade/downgrade over an existing save from older format;
- permission denial, permanent denial, request during suspend, and callback
  after requesting scene unloads;
- rapid pause/resume, surface destroyed without process death, process death
  with saved state, orientation during loading, keyboard over safe-area UI,
  audio-device loss, network-interface change, and low-memory during decode;
- OpenGL ES/Vulkan shader variant missing or texture decode differing from
  Linux;
- package runs with no network, no external storage, non-English locale, and
  system font assumptions unavailable.

### Release gates

- clean-checkout reproducible Linux and Android builds;
- package-content allowlist and dependency/reachability audit;
- automated install/launch/terminate/relaunch with save verification;
- lifecycle loop and low-memory tests with stable resource counts;
- capability/build report archived with every release artifact;
- documented rollback and save compatibility policy before declaring a release
  workflow stable.

### Done when

- One project produces a runnable Linux bundle and signed Android APK/AAB from
  documented CI commands.
- Platform metadata is project data, not a modified engine template.
- Device tests catch renderer and asset differences before manual testing.

## Step 10 — Performance and Reliability as Product Features

Developers should be able to identify the responsible subsystem before
changing game code or the engine.

**Status: planned.** CPU profiling, CSV reports, renderer statistics, debug
overlays, replay, and ownership failure suites already exist. Coverage and
budget enforcement are not yet uniform across assets, UI, Lua allocation,
audio, networking, GPU passes, and packaged Android lifecycle scenarios.

### Measurement architecture

- `ProfilerRegistry` owns stable counter/scope descriptors, frame samples,
  aggregation windows, and capture lifecycle. Subsystems publish values; they
  do not format HUD/CSV output.
- CPU scopes, counters, and GPU timings are different metric types with
  explicit units and availability. Missing GPU timing is reported as
  unavailable, never as zero.
- `PerformanceBudgetEvaluator` consumes captured metrics plus versioned project
  budget data and produces shared diagnostics for HUD, CLI, CI, and editor.
- `DiagnosticSnapshot` correlates immutable summaries from scene, script,
  assets, physics, render, audio, UI, network, and application lifecycle
  owners. It does not expose third-party pointers or sensitive payload data.
- Benchmark fixtures are representative committed source projects/replays with
  declared hardware/backend context. Optimizations must cite one fixture and
  retain its before/after evidence.

### Budget format

```json
{
  "format_version": 1,
  "profiles": {
    "android_mid": {
      "frame.p95_ms": {"max": 16.67},
      "lua.p95_ms": {"max": 2.0},
      "render.draw_calls.max": {"max": 800},
      "assets.resident_mb.max": {"max": 512},
      "ui.layout.p95_ms": {"max": 1.0},
      "network.out_kbps.p95": {"max": 128}
    }
  }
}
```

Budgets specify warm-up, sample length, percentile/aggregate, allowed backend,
and whether unavailable metrics skip or fail. CI comparisons use absolute
budgets first; noisy percentage-only regression gates are secondary.

### Deliverables

1. Add hierarchical CPU scopes and GPU/render-pass timing where supported.
2. Report Lua time/GC, allocations, asset memory, audio voices, network
   bandwidth, physics contacts, animation work, particles, UI nodes, batches,
   draw calls, and triangles.
3. Add per-project budgets and warnings for frames, loading, memory, entities,
   contacts, particles, lights, UI, and network traffic.
4. Allow deterministic performance gates in `demi test` and CI reports.
5. Add soak tests for create/destroy, pooling, scene replacement, additive
   scenes, resource reload, save migration, connect/disconnect, and Android
   lifecycle events.
6. Run ownership stress suites under ASan/LSan and add platform-appropriate
   thread/undefined-behavior checks.
7. Add diagnostic snapshots that correlate the active scene, scripts, asset
   groups, network peers, renderer, and recent lifecycle events.
8. Optimize only measured hot paths and preserve representative benchmarks for
   every accepted optimization.

### Implementation slices

#### 10A. Metric catalog

1. Define stable names, units, descriptions, owners, and collection cost for
   every metric.
2. Instrument frame/update/fixed/Lua callbacks, GC/allocation where measurable,
   physics steps/contacts/queries, UI mutation/layout/text, scene preparation,
   asset IO/decode/upload/residency, render extraction/batching/passes,
   animation/particles, audio voices/mixing, and network encode/traffic/buffer.
3. Compile or sample expensive metrics only when a capture/debug mode requests
   them.
4. Expose the catalog through CLI JSON and generated documentation.

#### 10B. Capture and reporting

1. Use bounded ring buffers and explicit capture start/stop; profiling cannot
   grow memory for an unbounded session.
2. Report mean, median, p95, p99, maximum, count, and longest-frame correlation
   where meaningful.
3. Export machine-readable JSON/CSV plus a concise human report using the same
   sample data.
4. Add frame markers for scene/load/reload/network/lifecycle events so spikes
   can be correlated without parsing logs.

#### 10C. Budget evaluation

1. Parse/validate project budget profiles and select them explicitly from CLI.
2. Separate warm-up from measured frames and report insufficient samples.
3. Produce a diagnostic per failed metric containing measured value, limit,
   aggregate, sample count, and capture context.
4. Store baseline reports only for stable deterministic fixtures; never hide a
   failed absolute budget by updating a baseline automatically.

#### 10D. Reliability and soak harness

1. Express lifecycle actions as deterministic commands: load/prepare/cancel,
   spawn/release, reload, save/migrate, connect/disconnect, pause/resume,
   low-memory, and surface recreate.
2. Combine bounded randomized sequences with recorded seeds and shrink/failure
   reproduction output.
3. Capture counts of live entities, scripts, callbacks, assets by owner, GPU
   handles, physics bodies, audio voices, peers, UI nodes, and allocated bytes
   before/after cycles.
4. Run selected sequences under ASan/LSan/UBSan and platform-specific tooling.

### Failure and edge-case matrix

- nested/recursive profiler scopes, scope exits through exceptions, duplicate
  names with wrong units, counter reset, and thread handoff;
- frame stalls longer than the capture window, timer wrap/precision, zero
  frames, pause/time-scale, and headless Noop backend;
- GPU timing unavailable, delayed by several frames, device reset, or multiple
  camera views;
- percentile calculation with few samples and capture crossing scene reload;
- profiler overhead changes the budget result or exceeds its own budget;
- Lua GC spike, asset decode/upload spike, shader compilation accidentally at
  runtime, UI relayout loop, physics contact storm, particle exhaustion,
  network resend storm, and audio voice thrashing;
- failure snapshot while another subsystem is tearing down;
- soak test failure must print seed, minimal command prefix, current owners,
  and last lifecycle events;
- user/network payloads and filesystem secrets must be redacted from reports.

### Required budget suites

| Suite | Primary risks |
|---|---|
| Visual novel | glyph/layout cache, backlog virtualization, image/voice transitions, save latency |
| 2D action | physics contacts/queries, projectiles/pooling, particles, draw batching |
| Isometric builder | pathfinding, entity count, UI updates, wave spawning |
| Lightweight 3D | mesh/texture residency, culling/instancing, lights/shadows, Jolt step |
| Multiplayer shooter | encode/decode, bandwidth, interpolation, peers/entities, server tick |
| Scene/resource stress | cancellation, shared ownership, reload, callback teardown, leaks |

Every suite declares debug and release expectations separately. Performance
numbers from a debug build may diagnose behavior but cannot certify a shipping
budget.

### Done when

- A developer can tell whether a slowdown is caused by Lua, rendering,
  physics, assets, UI, audio, or networking using shipped tools.
- Reference scenarios meet checked Linux and Android budgets.
- Repeated lifecycle tests show no leaks, dangling handles, or unbounded
  resource growth.

## Scenario Paths

The steps are cumulative, but developers can follow the shortest path for the
kind of game they are building:

| Scenario | Required roadmap steps | Result |
|---|---|---|
| Visual novel or dialogue-heavy RPG | 1–3 | Data-driven story, responsive dialogue UI, choices, audio, localization, and saves |
| Platformer or metroidvania | 1–4 | Template plus reusable movement, combat, checkpoint, camera, and content packages |
| Top-down action or puzzle game | 1–4 | Input, navigation, weapons/interactions, prefabs, saves, and responsive UI |
| Isometric builder or tactics game | 1–4 | Existing grid/path systems plus reusable objectives, units, UI, and content data |
| Lightweight 3D exploration/action | 1–3 and 5 | Predictable import, controller, physics, animation, materials, lighting, and diagnostics |
| Multiplayer action game | Relevant local-game path plus 6 | Lobby, authority, replication, late join, reconnect, testing, and server packaging |
| Large content-driven game | Relevant gameplay path plus 7 | Streaming groups, incremental cooking, hot reload, and reusable packages |
| Visual authoring workflow | 1–7, then 8 | Editor acceleration without hidden data or duplicate runtime behavior |
| Public Linux/Android release | Relevant game path plus 9–10 | Reproducible packages, device validation, profiling, and stability gates |

### Scenario A — Visual novel

Developer workflow:

1. Run `demi new my_story --template visual-novel`.
2. Replace template characters, backgrounds, music, and fonts through normal
   asset manifests.
3. Add chapters as schema-validated `DataAsset` documents with stable node and
   line IDs.
4. Add localized strings independently of layout and story topology.
5. Configure the reusable dialogue package and theme rather than editing its
   traversal code.
6. Preview long/short/CJK/RTL pseudo-locales at desktop and phone viewports.
7. Test every choice path, invalid reference, save point, migration, auto/skip,
   voice-missing fallback, and touch/keyboard/controller input headlessly.
8. Profile backlog/text cache and transition memory, then package Linux and
   Android from the same story data.

Acceptance project:

- at least two chapters and two backgrounds;
- three layered characters with expression changes;
- a conditional choice depending on earlier state;
- music crossfade, voiced and unvoiced lines, and one cutscene;
- English plus a pseudo-localized expansion locale;
- save on ordinary line and choice, quick-save/load, auto-save on chapter, and
  migration after one node rename;
- backlog virtualization, auto, skip-seen, hide UI, history, and settings;
- automated traversal proves every reachable node terminates or intentionally
  loops and every referenced asset/localization key exists.

No-engine-edit gate: adding a chapter, character, expression, choice operator
already supported by the package, language, or save slot changes no C++ and no
shared package internals.

### Scenario B — Platformer or top-down action game

Developer workflow:

1. Create the corresponding template and choose/install only the movement,
   health/damage, projectile, checkpoint, interaction, and camera packages the
   game needs.
2. Author player/enemy/pickup/projectile prefabs and tuning `DataAsset` files.
3. Build tilemaps/object layers and declare collision/navigation metadata.
4. Connect package events to game-specific score, animation, audio, and level
   progression policy.
5. Record deterministic input replays for movement, damage, death, checkpoint,
   scene transition, and save/load.
6. Stress projectile speed, pooling, simultaneous contacts, runtime tile edits,
   pause/resume, and aspect/touch variants.
7. Set budgets for physics, Lua, particles, batches, assets, and UI.

Acceptance project:

- keyboard/gamepad/touch actions use the same controller script;
- two enemy behaviors share damage/health without inheriting a common engine
  class;
- hitscan and pooled physical projectile modes both handle triggers/solids;
- checkpoints cross scene boundaries and restore a versioned save;
- dynamic tile or blocker updates render, physics, and navigation together;
- destruction during callbacks and release/reclaim in one frame are tested.

No-engine-edit gate: a new weapon, enemy, pickup, room, or tuning profile uses
prefab/data/package extension points only.

### Scenario C — Isometric builder or tactics game

Developer workflow:

1. Start from the isometric template with grid, occupancy, projection,
   navigation, selection, and placement already connected.
2. Define buildings/units/terrain/economy as data and prefabs.
3. Compose optional health, interaction, objective, wave, and save packages.
4. Keep build rules, turn/economy policy, targeting, AI, and victory conditions
   in game modules.
5. Validate footprint, path preservation, unreachable goals, stable sorting,
   and save references before play.
6. Replay large waves/turns with dynamic blockers and profile pathfinding,
   entity count, draw batches, and UI updates.

Acceptance project:

- rectangular and multi-cell footprints;
- placement preview with deterministic rejection reason;
- multiple agents/factions and at least two movement costs;
- path invalidation while agents are moving;
- selection and command input on mouse, controller focus, and touch;
- save/load during a wave/turn with stable prefab/entity IDs;
- hundreds of units/buildings remain within declared budgets.

No-engine-edit gate: a new building, unit, map, objective, targeting strategy,
or economy rule remains project data/Lua.

### Scenario D — Lightweight 3D exploration or action game

Developer workflow:

1. Create the lightweight-3D template and import a third-party glTF with an
   explicit profile.
2. Inspect normalized nodes/materials/animations/bounds and accept generated
   collider recommendations.
3. Compose character/camera/environment/pickup/projectile prefabs from public
   components and packages.
4. Use diagnostic views to correct source import/material data rather than
   adding model-specific renderer branches.
5. Replay grounding, slopes, steps, moving platforms, triggers, projectile
   casts, pause, and scene transitions.
6. Test culling, instancing, lights/shadows, textures/alpha, animation, and
   resource reload on Noop/Linux/Android paths as applicable.
7. Enforce mobile and desktop budgets before expanding the scene.

Acceptance project:

- mixed primitive meshes plus one skinned animated character;
- explicit non-default source axis/scale conversion;
- static triangle mesh, dynamic convex, trigger, and capsule controller;
- moving/rotating platform, slope/step, pickup, projectile, and door;
- multiple material texture modes including alpha cutoff;
- directional/point/spot light within declared limits;
- load/unload/reload cycles do not retain GPU/Jolt/animation resources.

No-engine-edit gate: replacing the character or environment model, adding a
clip/material/light/pickup, or changing collider detail is asset/prefab data.

### Scenario E — Multiplayer action game

Developer workflow:

1. Build and test the complete offline/local game path first.
2. Declare replicated prefabs, permitted fields, RPC schemas, rates, and
   authority rules.
3. Compose the lobby/session package and explicit scene-readiness policy.
4. Run a local headless server plus clients with deterministic normal, latency,
   loss, reorder, duplication, and disconnect profiles.
5. Test late join, ownership transfer, reconnect, scene transition, and server
   shutdown before adding prediction.
6. Add prediction/reconciliation only to measured latency-sensitive state and
   compare authoritative replay results.
7. Enforce payload, rate, bandwidth, server-tick, entity, and buffer budgets.
8. Package headless Linux server and Android/Linux clients from one project.

Acceptance project:

- lobby, ready state, match scene, results, and rematch/reset;
- at least two replicated prefab types and two authority models;
- reliable discrete events plus unreliable state/input flow;
- late join receives all current entities/session state automatically;
- disconnect removes/transfers exactly the correct state;
- invalid/unauthorized/oversized messages are rejected and counted;
- deterministic network simulations reproduce their result from a saved seed.

No-engine-edit gate: adding a replicated prefab, RPC, lobby field, map, or game
mode uses metadata, schema, prefabs, and Lua policy.

### Scenario F — UI-heavy management or RPG game

Developer workflow:

1. Model items, characters, quests, recipes, and balance values as validated
   data assets.
2. Build reusable UI prefabs and virtualized lists/grids projected from that
   data.
3. Keep domain state in explicit Lua models and saves; UI nodes remain views
   with typed commands/events.
4. Test keyboard/controller/touch focus, search/filter, drag/drop, modal state,
   locale expansion, safe areas, and save migrations.
5. Profile layout/text/list virtualization with production-scale fixture data.

Acceptance project:

- thousands of item definitions and a virtualized inventory;
- equipment, quest, settings, confirmation modal, and searchable list screens;
- focus restoration after filtering/removal and correct multi-pointer capture;
- versioned saves reference stable content IDs and diagnose removed content;
- no per-frame rebuilding of unchanged UI or content tables.

No-engine-edit gate: adding content fields within declared schemas, screens,
filters, commands, or localization remains data/Lua/UI work.

## Delivery Order

Implement the steps in this order unless a reference game demonstrates a
blocking dependency:

```text
1 Project workflow
  -> 2 Game data
    -> 3 Text/UI and visual novels
      -> 4 Reusable 2D/isometric kits
      -> 5 Lightweight 3D workflow
      -> 6 Multiplayer workflow
        -> 7 Assets, streaming, and packages
          -> 8 Editor
            -> 9 Shipping
              -> 10 Performance and reliability
```

Steps 4, 5, and 6 may proceed independently after Step 3. Performance tests
and failure coverage from Step 10 are added throughout the roadmap rather than
postponed until the end.

## Explicit Deferrals

Do not prioritize these without evidence from a reference or shipped game:

- high-end photorealistic rendering or Unity feature parity;
- open-world terrain and foliage authoring;
- shader graphs or visual scripting;
- a second editor-only asset or scene database;
- a mandatory all-purpose gameplay framework;
- built-in accounts, commerce, global matchmaking, anti-cheat, or live-service
  hosting;
- console platform support;
- arbitrary native-plugin ABI stability before the in-tree extension boundary
  is proven;
- replacing the runtime object model with a speculative ECS framework.

## Overall Completion Rule

The roadmap succeeds when developers can create, validate, run, test,
diagnose, package, and maintain the supported game scenarios using documented
project data, assets, Lua packages, and public APIs. Ordinary game requirements
must not require edits to engine internals, and every reusable workflow must be
proven on Linux and Android where the platform supports it.
