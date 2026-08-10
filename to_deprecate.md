# DemiEngine Deprecation Ledger

Last audited: 2026-08-10

## Purpose

This file is the single backlog for compatibility paths that should eventually
leave DemiEngine. Its purpose is to prevent every migration from leaving a
second permanent way to perform the same job.

An entry in this file is not, by itself, a formal deprecation. Public APIs and
formats still follow `docs/compatibility.md`: announce the deprecation for at
least one minor release, identify a replacement and removal version, provide a
deterministic migration where durable data is involved, and keep focused tests
until removal.

## Status Vocabulary

- **Candidate:** duplicated or obsolete behavior found during the audit, but
  the project has not started its compatibility window.
- **Blocked:** the intended replacement is incomplete or examples still need
  migration. Do not emit a deprecation warning yet.
- **Ready:** the replacement is complete, documented, tested, and used by all
  checked-in examples and templates. A release may begin formal deprecation.
- **Deprecated:** diagnostics and documentation name a removal version.
- **Remove:** the compatibility window has elapsed and the removal checklist
  is satisfied.

No entry may move directly from Candidate or Blocked to Remove.

## Required Retirement Workflow

For every item below:

1. Finish and document one canonical replacement.
2. Add a deterministic usage scanner to `demi validate`, `demi doctor`, or the
   capability gate as appropriate.
3. Migrate templates, examples, tests, documentation, and generated Lua stubs.
4. Add a stable deprecation diagnostic containing the old surface, replacement,
   and intended removal version.
5. Retain compatibility tests during the announced window and add migration
   tests for durable data.
6. Remove the old parser branch, binding, storage, tests, and documentation in
   the same change after the window expires.
7. Update the public capability baseline so the old surface cannot return by
   accident.

Warnings should be emitted once per source location or API per run, not once
per frame.

## Confirmed Public Deprecation Candidates

### DEP-001 — Compatibility UI callbacks

- **Status:** Candidate.
- **Old surface:** Lua `on_ui_hover(event)` and `on_ui_click(event)` callbacks.
- **Canonical replacement:** `on_ui_event(event)` or a typed callback such as
  `on_ui_pointer_enter`, `on_ui_pointer_exit`, `on_ui_press`, and
  `on_ui_release`.
- **Why:** the compatibility callbacks collapse several pointer states and
  cannot represent cancellation, pointer identity, drag/drop, focus, or touch
  correctly. Maintaining both dispatch paths makes UI behavior harder to test.
- **Current dependencies:** `LuaScriptHostServices.cpp`, generated Lua stubs,
  Lua scripting tests, and the Lua API documentation still expose them.
- **Ready when:** every example uses typed events; validation can detect the old
  callback names in scripts; typed callbacks have mouse, multitouch, capture,
  cancellation, hidden-node, and removal-during-callback coverage.

### DEP-002 — The `hud_action` event channel

- **Status:** Candidate.
- **Old surface:** `Events.subscribe("hud_action", ...)`.
- **Canonical replacement:** typed `ui_event`/`ui_<type>` events for interaction
  lifecycle and `---@handle_action` for simple authored control actions.
- **Why:** `hud_action` is a second action-delivery path with a smaller payload
  and ambiguous ordering relative to typed UI events.
- **Current dependencies:** compatibility dispatch in
  `LuaScriptHostServices.cpp` and Lua scripting regression tests.
- **Ready when:** action ordering and cancellation are documented for the typed
  path, the annotation handler covers project-wide action routing, and no
  checked-in script subscribes to `hud_action`.

### DEP-003 — Imperative `Hud.text` and `Hud.rect` construction

- **Status:** Blocked.
- **Old surface:** `Hud.text(...)` and `Hud.rect(...)`.
- **Canonical replacement:** retained HUD documents, parameterized UI prefabs,
  and `Hud.create`/`Hud.clone` for genuinely dynamic nodes.
- **Why:** these calls combine construction, replacement-by-ID, layout,
  presentation, and a legacy text-scale conversion. They bypass the richer
  retained-node contract and encourage hard-coded canvas coordinates.
- **Current dependencies:** `fighting_game_2d`, `minimal_voxel`, and
  `production_2d_foundation`, plus Lua tests and the LaTeX manual.
- **Prerequisite:** make `Hud.create` accept the full validated node definition
  used by HUD JSON, or make runtime prefab instantiation sufficient for these
  examples. Dynamic nodes must retain generation-checked ownership and clear
  teardown behavior.
- **Ready when:** the listed examples contain no `Hud.text` or `Hud.rect`, and
  repeated create/remove cycles are covered by lifetime and layout tests.

### DEP-004 — Redundant HUD text mutators

- **Status:** Blocked.
- **Old surface:** `Hud.set_button_label(id, value)` and
  `Hud.set_text_scale(id, scale)`.
- **Canonical replacement:** `Hud.set_text` for all textual controls and a new
  consistently named font-size/style mutation API expressed in HUD units.
- **Why:** button labels are ordinary node text, while `set_text_scale` applies
  a hidden `scale * 8` conversion that does not match authored `font_size`.
- **Current dependencies:** animation selection and UI showcase code use
  `set_button_label`; scripting tests use both functions.
- **Ready when:** `Hud.set_text` handles every textual node type, the explicit
  font-size replacement exists, and all examples/stubs/docs have migrated.

### DEP-005 — `Runtime` as a mixed-responsibility Lua service

- **Status:** Blocked.
- **Old surface:** the `Runtime` Lua table.
- **Canonical replacement:** move process/window/platform functions to
  `Application`, simulation controls to `Time` or a narrow simulation service,
  and physics enablement to a physics-facing service.
- **Why:** `Runtime` currently mixes quit, platform inspection, physics,
  window mode, frame limiting, mouse capture, focus, and suspension. Some
  state is already duplicated by `Application`.
- **Current dependencies:** most substantial examples use at least one Runtime
  function. This is an architectural consolidation, not an immediate removal.
- **Prerequisite:** define the destination of every function first. Do not
  replace one grab-bag with another and do not expose SDL-specific concepts.
- **Ready when:** replacement APIs preserve Linux/Android behavior, examples no
  longer use `Runtime`, and the generated stubs mark each forwarding alias with
  its individual replacement.

### DEP-006 — Input-action document shorthands

- **Status:** Blocked.
- **Old surface:**
  - an action defined directly as an array instead of an object;
  - string binding entries instead of `{ "input": ... }` objects;
  - the binding field alias `key` instead of `input`;
  - action type aliases `axis`, `1d`, `vector`, and `2d`;
  - unqualified keyboard controls such as `space` instead of `key:space`;
  - implicit `axis1d` inference from a non-default binding scale.
- **Canonical replacement:** object-form actions with explicit `type`,
  `context`, and object-form bindings using canonical device-qualified input
  names. Canonical types are `button`, `axis1d`, and `vector2`.
- **Why:** the parser currently accepts several shapes and guesses both device
  and action type. That weakens validation, migration, rebinding, and generated
  documentation.
- **Current dependencies:** templates and many example project files still use
  the shorthand forms, so this item must not be warned on until they migrate.
- **Ready when:** a format migration rewrites every shorthand deterministically,
  all templates/examples use the canonical shape, and validation rejects
  conflicting explicit/implicit meanings.

### DEP-007 — Raw keyboard-combining helpers

- **Status:** Candidate.
- **Old surface:** `Input.axis(negative, positive)` and
  `Input.vector(left, right, down, up)`.
- **Canonical replacement:** declared input actions consumed with
  `Input.action_value` and `Input.action_vector`.
- **Why:** the helpers hard-code keyboard controls in gameplay scripts and
  duplicate deadzone, inversion, player assignment, context, virtual-control,
  and rebinding behavior.
- **Scope note:** low-level key, mouse, touch, and gamepad inspection remains a
  valid tool/debug/rebinding capability and is not covered by this item.
- **Ready when:** examples use actions for gameplay intent and action-vector
  tests cover keyboard, gamepad, touch, multiple players, and rebinding.

### DEP-008 — Numeric 3D animation clip selection

- **Status:** Candidate.
- **Old surface:** `AnimationPlayer3D.clip` and animation-state
  `model_clip` numeric indexes.
- **Canonical replacement:** `clip_name` and `model_clip_name` using stable
  imported clip names.
- **Why:** numeric clip order is an importer detail and can change when a model
  is re-exported. Stable names survive asset reimport and are already used by
  `animation_3d`.
- **Current dependencies:** compatibility parsing/runtime branches and
  animation primitive tests still exercise numeric clips.
- **Ready when:** missing and duplicate clip names are validated at import and
  scene load, every example uses names, and a migration can resolve old indexes
  against the referenced model or report that it cannot do so safely.

### DEP-009 — Constant-rate root-motion fallback

- **Status:** Candidate.
- **Old surface:** animation-state `root_motion_per_second`.
- **Canonical replacement:** sampled `root_motion_track` with an explicit state
  duration.
- **Why:** the constant vector cannot reproduce authored animation motion and
  keeps a second integration branch in the animation state-machine system.
- **Ready when:** import/cooking can produce tracks for supported models,
  migration can generate an equivalent two-sample linear track, and tests cover
  looping, non-looping, blending, pause, and large time steps.

### DEP-010 — `Entity.set_sprite_color`

- **Status:** Blocked.
- **Old surface:** `Entity.set_sprite_color(...)`.
- **Canonical replacement:** a component-owned `Sprite2D.set_color(...)` API,
  with `Entity.set(...)` remaining the generic reflective escape hatch.
- **Why:** one component-specific mutation on the generic Entity service is an
  ownership exception and duplicates the component binding pattern.
- **Ready when:** `Sprite2D.set_color` exists, has the same clamping/error
  behavior, all examples use it, and the Lua stub/capability manifest reflects
  the move.

### DEP-011 — CLI `--frames` alias

- **Status:** Candidate.
- **Old surface:** `demi run ... --frames <count>`.
- **Canonical replacement:** `--max-frames <count>`.
- **Why:** both flags set the same limit. Keeping both expands documentation,
  parsing, and testing without adding a distinct workflow.
- **Current dependencies:** README, CLI documentation, CMake example tests,
  and runtime help currently advertise or use `--frames`.
- **Ready when:** all maintained commands use `--max-frames`, the CLI emits one
  deprecation diagnostic for `--frames`, and direct `demi-runtime` invocation
  follows the same policy.

## Format and Import Compatibility Candidates

These need longer retention than ordinary Lua aliases because they may be the
only way to recover old authored data.

### DEP-012 — Incomplete version-1 asset-manifest reimport shim

- **Status:** Candidate; recovery path, retain conservatively.
- **Old surface:** manifests lacking `importer`, `importer_version`,
  `source_hash`, dependencies, or settings and accepted only through
  `demi asset reimport`.
- **Canonical replacement:** complete versioned manifests produced by
  `demi asset import`/`register-generated` and maintained by reimport.
- **Why:** the shim guesses importer metadata from file extensions. It should
  not remain an unbounded promise to reconstruct every historical manifest.
- **Removal gate:** only after the minimum supported source format advances,
  an archived migration tool or release can still perform the conversion, and
  recovery documentation names the last supporting engine version.

### DEP-013 — Generic `json-data` importer compatibility route

- **Status:** Blocked.
- **Old surface:** arbitrary non-`DataAsset`/`DataSchema` JSON types routed
  through the generic `json-data` importer name.
- **Canonical replacement:** `json_data` for game data and explicitly
  registered typed importers for material, shader, tilemap, scene-adjacent, or
  other structured assets.
- **Why:** one generic JSON importer hides which subsystem owns validation,
  dependencies, cooking, and version upgrades.
- **Current dependencies:** `data_asset_tests.cpp` explicitly protects the
  legacy route.
- **Ready when:** every supported JSON asset type has an owning importer or is
  deliberately classified as `DataAsset`, and reimport migrates the importer
  name without changing the asset ID.

### DEP-014 — P3/P6 PPM runtime image compatibility

- **Status:** Blocked.
- **Old surface:** `.ppm` import and runtime decoding.
- **Canonical replacement:** alpha-capable packaged formats such as PNG or QOI;
  tests that need tiny deterministic images should construct RGBA fixtures
  directly or generate files under the test build directory.
- **Why:** PPM exists mainly as a hand-authored migration/test convenience and
  keeps a separate parser in the runtime decoder. Several examples still ship
  PPM assets.
- **Ready when:** all checked-in example PPM files are migrated with unchanged
  visual output, importer/cooker tests no longer rely on PPM as their generic
  fixture, and package compatibility policy permits dropping the decoder.

### DEP-015 — Texture `color_key`

- **Status:** Blocked.
- **Old surface:** texture setting `color_key` and post-decode exact-RGB
  transparency conversion.
- **Canonical replacement:** source images with authored alpha.
- **Why:** exact color-key transparency is a compatibility feature for RGB-only
  sprites and creates edge/bleed problems under filtering and mipmapping.
- **Current dependencies:** networking and Android examples still use black
  color keys for player and coin PPM assets.
- **Ready when:** those sources are replaced with alpha textures, importer
  tooling offers an explicit one-time conversion, and regression images cover
  nearest, bilinear, mipmapped, and atlas-packed edges.

### DEP-016 — Panel `color` as a background fallback

- **Status:** Blocked.
- **Old surface:** HUD panels using `color` for their fill when
  `background_color` is absent.
- **Canonical replacement:** `background_color` for panel/control fills;
  `color` remains foreground text/shape color or image tint where those
  meanings are explicit.
- **Why:** the meaning of `color` currently changes with node type, while
  styles already have an explicit background field. This makes themes,
  mutation APIs, schema help, and renderer behavior less predictable.
- **Current dependencies:** most older HUD documents use `color` for panels and
  buttons, while newer themed documents already use `background_color`.
- **Ready when:** a HUD migration rewrites visual panel/control colors without
  changing label or image-tint semantics, every example has screenshot/layout
  regression coverage, and validation can distinguish an intentional
  transparent background from an omitted one.

### DEP-017 — Parallel HUD `group` visibility

- **Status:** Blocked.
- **Old surface:** the HUD node `group` field and
  `Hud.set_group_visible(name, visible)`.
- **Canonical replacement:** parent/child hierarchy with visibility changed on
  the owning panel or container.
- **Why:** `set_group_visible` currently treats a matching node ID, parent,
  group, or style as the same selection. That creates a second hierarchy and
  makes a style name capable of changing runtime ownership/visibility. Nested
  panels now provide the explicit structural grouping this predates.
- **Current dependencies:** isometric, networking, Android, voxel, shooter, and
  minimal-3D HUDs still use groups.
- **Ready when:** those HUDs have structural group roots, hiding a parent has
  deterministic focus/capture cancellation for descendants, and validation
  rejects accidental parent cycles and ambiguous visibility targets.

## Internal Compatibility State to Remove

Internal entries do not need a public minor-release window, but they still
need focused regression coverage before removal.

### DEP-018 — Desktop pointer-zero capture mirror

- **Status:** Candidate.
- **Old surface:** `UiDocument.pointerCaptureId`, maintained beside
  `pointerCaptures[0]` for source compatibility.
- **Canonical replacement:** the per-pointer capture, hover, position, press,
  and drag collections.
- **Why:** two sources of truth can diverge and special-case desktop input in
  event delivery, invalidation, and tests.
- **Ready when:** every caller reads pointer zero through the same per-pointer
  API, cancellation/removal clears one representation, and tests cover mouse
  plus simultaneous touch captures.

## Already Removed or Rejected — Do Not Reintroduce

These are not active deprecations. They are recorded because future work must
not recreate adapters for them:

- raylib/rlgl renderers, resource types, and filesystem bridge;
- dual raylib/bgfx runtime selection;
- shader `platform_sources` and `platform_fallbacks`; use one bgfx `.sc` source
  set and cooked backend binaries;
- runtime platform-specific game shader source selection;
- editor-only durable state that is absent from versioned project data.

If old external content needs one of these, provide an offline migration tool
instead of restoring the runtime path.

## Intentionally Not Deprecated

The following are variations with distinct jobs, not accidental legacy:

- Vulkan, OpenGL, OpenGL ES, Automatic, and Noop `GraphicsDevice` selection;
- material and shader fallbacks used for capability-safe rendering;
- fallback fonts and missing-glyph handling;
- explicit save migrations and versioned source-data migrations;
- low-level input inspection needed by rebinding, tools, diagnostics, and
  pointer-driven gameplay;
- typed component bindings alongside reflective `Entity.get`/`Entity.set`;
- headless/Noop rendering paths used by tests and dedicated servers.

## Suggested Execution Order

1. Add deprecation metadata and usage diagnostics without removing behavior.
2. Canonicalize project input documents and named animation clips.
3. Finish retained/dynamic HUD mutation APIs, canonicalize panel backgrounds
   and structural visibility groups, and migrate all examples.
4. Consolidate Lua service ownership (`Runtime`, sprite color, UI events).
5. Convert PPM/color-key assets and retire their decoder/import settings.
6. Advance the supported source-format floor and archive old reimport shims.
7. Remove the CLI alias and internal pointer mirror after their consumers are
   gone.

The order is dependency-driven: checked-in examples must demonstrate the
canonical path before compatibility code starts warning, and warnings must
exist before removal.
