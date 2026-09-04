# Plan: Improvements To Code Design (Convention Over Configuration)

Goal: reduce developer-authored JSON/Lua needed for common cases in HUD,
scene/prefab, project/input, and Lua gameplay — without removing explicit
control or breaking existing files. Every proposal below is additive with
defaults: omitted fields get sensible values, verbose forms keep working.

Principles for all steps:

1. Defaults are applied at parse/binding time, never by rewriting author files.
2. Verbose form always still validates. `demi validate` must accept old + new.
   When a verbose form is superseded by a shorthand/preset, the old form keeps
   working but emits a `DEPRECATED_*` warning diagnostic (not an error) naming
   the replacement. Example gadgets are migrated to the new form so they stay
   warning-free; user projects get a clear migration path instead of silent rot.
3. Each slice updates together: parser, schema, `demi validate` diagnostics,
   Lua bindings, `scripts/stubs/demi.lua`, docs, tests.
4. No hidden state: resolved defaults must be inspectable
   (`demi scene inspect`, `demi hud inspect`, layout report).
5. Prefer small reusable engine defaults + Lua helpers over new mandatory systems.

## 0. Measured Friction (evidence)

HUD (`examples/ui_showcase/scenes/main.hud.json` 317 lines,
`examples/minimal_3d/scenes/main.hud.json` 84 lines,
`src/demi/runtime/ui/UiDocumentParser.cpp`, `schemas/hud.schema.json`):

- Every file repeats `format_version`, `canvas_size: [960,540]`, root
  `ui_root` container with `anchor_min [0,0]` / `anchor_max [1,1]`.
- Every label repeats `position`, `size`, `font_size`, 4-float `color`.
  Style (`UiDocumentParser.cpp:226-253`) only cascades
  `color/background_color/padding/gap` — not `font_size`, `size`, alignment.
- Buttons repeat identical `size: [250,44]` / `[250,40]` per node.
- Anchors use two 2-vectors for the common cases (fill, top bar, centered modal).
- Colors are always 4-float arrays; no `#rrggbb` / theme token.
- No auto-size: text nodes must hand-pick width/height or truncate.

Scene/prefab (`examples/minimal_3d/scenes/main.scene.json` 342 lines,
`examples/minimal_3d/prefabs/*.prefab.json`):

- Identity `Transform3D` repeats `rotation [0,0,0]` + `scale [1,1,1]` on ~7 entities.
- Every static body repeats full `Rigidbody3D`
  (`body_type`, zero `velocity`, `use_gravity:false`, `gravity_scale:0`) plus
  `wireframe:false`, `offset:[0,0,0]`, `is_trigger:false`.
- Prefab override path is deep:
  `overrides.body.components.Transform3D.position`.
- No named component presets (e.g. `static_box`, `trigger_sphere`).

Project/input (`examples/minimal_3d/demi.project.json`,
`templates/_shared/2d/demi.project.json`):

- `move_x`/`move_y` axis definitions (~10 lines each) copy-pasted across
  templates. No `preset: "wasd_arrows"` / `preset: "confirm"`.
- `performance_budgets` block (~10 keys) repeated per project.
- `scenes: [{id, path}]` duplicates information already in the scene file.

Lua (`examples/minimal_3d/scripts/player_3d.lua` 190 lines,
`examples/ui_showcase/scripts/ui_showcase.lua` 128 lines,
`scripts/stubs/demi.lua`):

- `Transform.add_position(self.entity_id, ...)` / `Hud.set_text(id, ...)`
  repeat the id on every call; no `self:`-bound shortcut.
- Diagonal normalization (`* 0.70710678`) hand-rolled in `player_3d.lua:70-75`.
- Projectile lifetime table + expiry sweep (`player_3d.lua:103-156`) +
  manual contact matching (`contact.entity_id` vs `contact.other_entity_id`).
- Manual subscription table + `on_destroy` unsubscribe loop
  (`player_3d.lua:40-59,180-188`, `ui_showcase.lua:75-95`).
- UI polling: `ui_showcase.lua:97-105` polls `Hud.get_text` every `on_update`
  to detect search change; scroll math (`max(#keys*38-300,0)`) hand-rolled;
  `Hud.recycle_rows` takes 7 positional args.
- Dropdown/tabs/modal reimplemented per screen (~40 lines in showcase).
- Jump-buffer + coyote timers hand-rolled in every character controller
  (`player_3d.lua:159-173`); should be a default.
- `Hud.set_text` call sites pass extra positional layout args
  (`player_3d.lua:44,60,64,95`) that the stub signature
  `(id, text) -> bool` does not declare — sign of an unclear/overloaded API.

---

## P1. HUD Defaults + Layout Shorthand

Problem: 60-80% of HUD JSON is repeated root/canvas/anchor/size boilerplate.

1. Default `canvas_size` to `[960,540]` when omitted. The default stays a
   resolution-independent logical size; per-project overrides remain explicit
   for phone/portrait or ultrawide layouts.
2. Allow omitting the `ui_root` wrapper: if top-level `root` is absent but
   `children`/`elements` present, synthesize `ui_root` fill container.
   Keep explicit root working (back-compat).
3. Add anchor presets resolved in `UiDocumentParser::parseNode` before
   `UiLayoutEngine::layout`:
   `dock: "fill"|"top"|"bottom"|"left"|"right"|"center"`,
   equivalent to the current `anchor_min/max + position/size` expansion.
   `demi hud inspect` prints the resolved rect so defaults are visible.
4. Add `stack: "column"|"row"` alias for `layout` + `gap`, and `pad: N`
   alias for uniform `padding`/`margin` (parser already accepts scalar in
   `insets()`, schema does not — fix schema).
5. Consequence: `minimal_3d/scenes/main.hud.json` shrinks from 84 to ~30 lines;
   showcase menu columns drop per-button `size` repetition via P2.

Before:

```json
{"id":"main","type":"container","anchor_min":[0,0],"anchor_max":[1,1],
 "children":[{"type":"label","id":"hud_label","text":"Minimal 3D",
 "position":[20,60],"font_size":32,"color":[0.85,0.92,1.0,1.0]}]}
```

After:

```json
{"id":"main","dock":"fill","children":[
  {"type":"label","id":"hud_label","text":"Minimal 3D","at":[20,60],
   "font":"title"}]}
```

Engine changes: `UiDocumentParser.cpp`, `hud.schema.json`
(`dock/stack/pad/at`), `HudLayoutReport`, `HudCommands inspect`,
`tests/ui_tests.cpp` + golden layout tests proving identical `resolved` rects.

## P2. Theme-Cascaded Control Defaults

Problem: styles don't cover `font_size`, control sizes, text colors; every
button/label repeats them.

1. Extend `UiStyle` with `font_size`, `text_color`, `min_size`,
   `control_height`, `row_height`. Parser cascade order:
   node field > named style > type default (`button.h=44`, `label.font=20`) >
   engine fallback. Never overwrite an explicitly authored field
   (current padding/gap guard pattern at `UiDocumentParser.cpp:248-252`).
2. Add theme tokens for color: `"color": "accent"` resolves via theme;
   keep float arrays working. Add `"color": "#RRGGBB[AA]"` parsing.
3. Add text auto-size: `size: "auto"` or omitted `size` on `label/button`
   measures via `TextLayoutEngine` with `max_lines + ellipsis` as the bound.
   Explicit `size` still wins.
4. Consequence: showcase `window_mode_*` buttons collapse to
   `{"id":..., "type":"button", "text":...}` + style once.

## P3. High-Level HUD Lua Helpers (biggest Lua saving)

Problem: list/filter/scroll/dropdown/tabs/modal require 30-60 lines of manual
`Hud.*` + `Events.*` bookkeeping.

Add thin Lua-side helpers (C++ stays narrow; helpers live in a checked-in
`scripts/runtime/demi/ui_helpers.lua` or generated bindings that call existing
`Hud.*` so no new ownership is introduced):

1. `Hud.bind_list(collection_id, template_id, keys, render_fn)` — wraps
   `find + recycle_rows + set_text`, computes `maximum_scroll` internally.
2. `Hud.bind_filter(search_input_id, items, key_fn, render_fn)` — replaces
   showcase `filter_inventory` (~25 lines) with change-callback instead of
   per-frame `on_update` polling. Requires new `Hud.on_change(id, fn)` event
   (typed UI event path, auto-unsubscribed on scene unload — fixes the manual
   `unsubscribe` loop).
3. `Hud.tabs({a="panel_a",...})`, `Hud.dropdown(button_id, options_id, items)`,
   `Hud.modal(show_id/hide_id)` — replace per-screen reimplementation with the
   existing `action_effects` show/hide/focus path.
4. `Hud.scroll_panel(panel_id, row_height, viewport)` helper owning
   `inventory_scroll` clamping + `ui_scroll` subscription.
5. Fix and document `Hud.set_text(id, text)` to exactly `(id, text)`;
   add `Hud.set_label(id, {text, at, size})` if positional placement is needed
   instead of the current undocumented extra args.

Showcase target: `ui_showcase.lua` 128 lines -> ~45 lines; no `on_update`
poll, no manual subscription table.

```lua
-- after
Hud.bind_filter("inventory_search", inventory_items,
  function(item) return item.id end,
  function(row, item) Hud.set_text(row.id, item.label) end)
Hud.tabs({ open_settings = "settings_panel", open_inventory = "inventory_panel" })
```

## P4. Self-Bound Entity Script Shortcuts

Problem: `self.entity_id` threaded through every call.

1. Add `self:`-style sugar with zero new ownership: in `LuaScriptHost`,
   expose a per-instance `self` proxy so scripts can write
   `self:move(x*dt, y*dt)`, `self:set_text("hud_label", msg)`,
   `self:on("physics3d_trigger_enter", fn)` with automatic cleanup on
   `on_destroy` (kills the manual subscription tables).
   Desugar to existing `Transform/Entity/Hud/Events` services.
2. Add `Input.vector("move")` returning normalized `{x,y}` (kills the
   `0.7071` diagonal hack) and `Input.pressed/down/value` short aliases
   documented in one place.
3. Consequence: template `main.lua` update becomes 2 lines;
   `player_3d.lua` movement block halves.

Before: `Transform.add_position(self.entity_id, x*self.speed*dt, y*self.speed*dt)`
After: `self:move(x*self.speed*dt, y*self.speed*dt)`

## P5. Scene Component Defaults + Presets

Problem: identity transforms and static-body boilerplate dominate scenes.

1. Apply component field defaults at parse: omitted `Transform2D/3D.rotation`
   -> zero, `scale` -> one, `Rigidbody.velocity` -> zero, `wireframe` ->
   false, `offset` -> zero, `is_trigger` -> false. Missing `name` defaults to
   `id`. Validate with `demi scene inspect` showing resolved values.
2. Add `preset` expansion in scene/prefab JSON, resolved before validation:
   `"preset": "static_box_3d"` expands to
   `BoxCollider3D + Rigidbody3D{static}` with `size` forwarded;
   `"preset": "trigger_sphere_3d"`, `"preset": "prop_2d"`,
   `"preset": "character_3d"`. Presets are data (checked-in JSON), not new
   C++ types; unknown preset = validation error with suggestion.
3. Flatten prefab overrides: allow
   `"overrides": {"body.Transform3D.position": [4,1,0]}` alongside the current
   nested form.
4. Consequence: `main.scene.json` ground/pillar/pickup entities drop from
   ~30 lines each to ~12; no behavior change.

```json
{"id":"ent_ground","preset":"static_box_3d","size":[40,0.2,40],
 "components":{"MeshRenderer":{"shape":"plane","size":[40,1,40]}}}
```

## P6. Entity Spawn + Projectile + Timer Helpers

Problem: `Entity.create` nested dict + manual lifetime/contact matching.

1. `Entity.spawn(prefab_or_spec, {position=..., velocity=..., ttl=...})` —
   positional shorthand for the `Transform3D.position` + `Rigidbody.velocity`
   boilerplate; `ttl` auto-destroys via existing `Timer` (removes the expiry
   sweep loop).
2. `Physics.on_trigger(entity, other, fn)` / `Physics.on_collision(fn)` typed
   helpers replacing manual `contact.entity_id/other_entity_id` matching.
3. `Timer.after(sec, fn)` auto-cancelled on entity destroy (already partially
   exists as `Timer.delay`; wire lifetime binding + document it as the
   default so scripts stop keeping `self.projectiles` tables).
4. `player_3d.lua` fire block target: ~40 lines -> ~12 lines.

## P7. Controller + Movement Packages As Defaults

Problem: jump-buffer/coyote/platform-follow reimplemented per game.

Move the already-proven patterns into optional Lua packages (consistent with
`packages/` direction) and reference them from templates:

- `demi.gameplay.movement_3d`: yaw move, normalized input, coyote + buffer,
  `CharacterController3D` wiring with `speed/jump_speed` tuning via
  `DataAsset` + `property_schema` (not new C++).
- `demi.gameplay.projectiles_3d`: pooled spawn, velocity, ttl, hit cleanup.
- 3D template `character_3d.lua` becomes composition + tuning, not physics code.
- Fixes belong in the package (per AGENTS.md probe rule), examples shrink.

## P8. Project/Input Presets + Budget Defaults

Problem: input + budgets copy-paste.

1. `input.presets: ["wasd_arrows", "confirm", "gamepad_confirm"]` in
   `demi.project.json`, expanded by `ProjectParser` into the current
   `actions` map. Explicit `actions` merge over presets. `demi validate`
   prints expanded actions.
2. Default `performance_budgets` when omitted (current minimal_3d values as
   the documented baseline); only deviations authored.
3. Infer `scenes[]` path from `id` when `path` omitted
   (`scene://ns/main` -> `scenes/main.scene.json`); keep explicit path working.
4. Consequence: `_shared/2d/demi.project.json` input block (~25 lines) -> 3 lines.

## P9. Property + Module Boilerplate Reduction

1. Compact property annotation: single-line
   `---@prop number(0,20) Move Speed` desugared to current
   `demi_property/label/range` triple — specified in P12, listed here for
   completeness. Old form keeps working.
2. Default script module resolution: `"module": "player_3d"` resolves to
   `script://scripts/player_3d.lua` when no `://` present.
3. `self.speed` etc. already default via `property_schema`; document the
   minimal `on_update`-only script as the canonical template (current 2D
   template is already close — keep it).

## P10. CLI Discoverability (prevents code by making defaults visible)

1. `demi new --list` + template READMEs show the minimal after-state for each
   preset (so devs discover P8/P5 instead of copying examples).
2. `demi hud inspect --resolved`, `demi scene inspect --resolved`,
   `demi validate --explain <code>` print which default/preset produced each
   value. Required so convention never becomes magic.
3. `demi lua-stubs generate` must include all new helpers (P3/P4/P6) on day one.

## P11. Asset-Bearing Content Packages Under `packages/`

Problem: `packages/` today ships pure Lua only. All 11 first-party packages
(`packages/sources/*`, see `packages/README.md`) declare `scripts/*.lua` +
`tests/` and nothing else — even though the manifest schema
(`schemas/package.schema.json`) and `PackageManifest.cpp:155` already support
`asset_manifests` and `engine_extensions`, and `PackageContent.h`
(`loadLockedPackageContent`) can load locked package content. Zero first-party
packages use those fields, so shipping reusable prefabs, UI kits, themes,
data schemas, and input presets through packages is designed but unproven.
Today every game re-authors the same button prefabs, character rigs,
projectile definitions, and item/quest schemas by copying examples.

1. Prove the mechanism with asset-bearing packages. Package layout extends to:
   `prefabs/`, `hud/` (ui-prefabs, themes), `data/schemas/` + `data/content/`,
   `input/` (named presets as data). Manifest lists them via `files` +
   `asset_manifests`; installed tree + lock remain the only runtime source
   (runtime never contacts a registry, per `packages/README.md`). Proposed
   first packages:
   - `demi.ui.kit`: `menu_button`-style ui-prefabs (button, modal, dropdown,
     list row, tabs), one base theme, wired to the P3 Lua helpers so
     `Hud.tabs/dropdown/modal/bind_filter` work out of the box.
   - `demi.gameplay.movement_3d` (+ `projectiles_3d`): character rig, projectile,
     pickup prefabs alongside the P7 Lua modules — template composes, package
     owns tuning via `DataAsset` + `property_schema`.
   - `demi.presets.input`: `wasd_arrows`, `confirm`, `gamepad_confirm` as
     versioned data consumed by the P8 `input.presets` expansion (presets as
     package content, not hardcoded C++).
   - `demi.content.starter_items` (example): item/quest/dialogue `DataAsset`
     schemas + sample content showing the schema-backed workflow without
     inventing loaders.
2. Rules (same as gameplay packages): stable IDs namespaced per package
   (e.g. `prefab://demi.ui.kit/button`) so `PackageInstaller` stable-ID
   collision checks stay meaningful; unknown/colliding IDs are validation
   errors with suggestions. Packages ship composition + tuning; game policy
   stays in game scripts. `demi validate` checks package assets transitively;
   cook/package include them via existing dependency keys; offline
   `install --locked` reproduces them byte-identically.
3. Consume from templates, don't copy: 2D/3D templates declare the kit +
   movement packages in `demi.project.json` instead of vendoring rig/prefab
   JSON. `demi package list --assets` (new) shows which prefabs/HUD/data each
   installed package provides; template READMEs point at it (feeds P10).
4. Consequence: new games get buttons, modals, character rigs, and input maps
   with 2 manifest lines instead of ~200 lines of copied JSON/Lua.

Engine changes: `PackageManifest`/`PackageInstaller`/`PackageContent` asset
wiring tests (cook, transitive deps, collision diagnostics), 1–2 reference
asset-bearing packages with `demi package test` coverage, schema + docs +
`packages/README.md` table update, template migration to depend (not copy).

## P12. Annotations That Remove Boilerplate

Problem: current annotations (`-- @HandleAction("...")`, `-- @OnEvent("...")`,
`---@demi_component`, `---@demi_property` + `@label/@range/@options`, see
`docs/script-properties.md` and
`src/demi/runtime/scripting/annotations/`) cover action dispatch and inspector
metadata, but the heaviest manual bookkeeping — `Events.subscribe` tables +
`on_destroy` unsubscribe loops (`player_3d.lua:40-59,180-188`,
`ui_showcase.lua:75-95`), per-frame UI polling, verbose property triples —
has no annotation. A misspelled annotation is also silently ignored today.

1. New markers, all desugared by the existing `LuaAnnotationScanner` path
   (thin adapters, no new ownership; auto-cleanup on scene unload/destroy):
   - `-- @Subscribe("event_name")` on a method: auto-subscribe with the method
     as callback, auto-unsubscribe on `on_destroy`/unload. Kills the manual
     subscription tables.
   - `-- @OnUi("action")` / `-- @OnUi({"a","b"})`: binds one method to UI
     action(s); companion to `@HandleAction`, documented as the default for
     buttons/dropdowns/tabs so P3 helpers + showcase need no dispatch tables.
   - `-- @BindHud("node_id")`: injects a generation-checked `Hud` handle/field
     at `on_start` (fails loudly with a diagnostic when the node is missing
     instead of nil-chasing at runtime).
   - Compact property form (from P9, owned here):
     `---@prop number(0,20) Move Speed` desugars to the
     `demi_property/label/range` triple. Old triple keeps working.
2. Diagnostics, not silence: unknown `@...` markers on script tables and
   dangling references (action/event/node that doesn't exist) surface through
   `demi script check` + `demi validate` with stable codes and suggestions.
3. Stubs + docs on day one: `scripts/stubs/demi.lua` documents each marker
   with before/after; `docs/script-properties.md` gains one page per marker;
   package modules resolve through the installed `.demi/packages` LuaLS path
   so `require("demi.gameplay.health")` + annotations compose.
4. Consequence: `player_3d.lua` subscriptions collapse to two annotated
   methods; `ui_showcase.lua` dropdown handler to one `@OnUi` method; property
   blocks drop from 4 lines to 1.

```lua
-- after
-- @Subscribe("physics3d_trigger_enter")
function Player3D:on_trigger(contact) ... end
-- @OnUi({"window_mode_windowed","window_mode_borderless","window_mode_fullscreen"})
function UiShowcase:on_window_mode(event) ... end
---@prop number(0,20) Move Speed
Player3D.speed = 6.0
```

Engine changes: `LuaAnnotationScanner` + `HandleActionAnnotation`/
`OnEventAnnotation`-style parsers, `LuaScriptHost` lifecycle wiring +
cleanup, `ScriptPropertyContract` compact form, `checkScriptSyntax` +
validation diagnostics, stub/docs/tests including a misspelled-annotation
regression test.

---

## Phasing

- Phase 1 (highest ROI, no format break): P1.3/P1.4, P2.1, P4, P8. Pure
  parser/binding defaults + input presets. Each is independently testable via
  golden resolved-rect / expanded-action tests.
- Phase 2: P3 helpers + `Hud.on_change` auto-cleanup, P5 presets + flattened
  overrides, P6 spawn/timer helpers. Requires stub + `ui_showcase` rewrite as
  the probe.
- Phase 3: P7 packages + P11 asset-bearing packages (`demi.ui.kit` first, then
  movement/projectile prefabs + input presets), P2.3 auto-size (needs
  renderer-independent measurement tests), P12 annotations
  (`@Subscribe`/`@OnUi`/`@BindHud`/compact `@prop`, incl. misspelling
  diagnostics), P10 `--resolved` inspect flags.

## Non-Goals

- No visual-scripting, shader graphs, or editor-only state (per product tiers).
- No removal of explicit forms; no silent behavior change for fully-specified files.
- No new third-party deps; shaping/layout stay behind existing text contracts.
- No engine-owned dialogue/inventory screens; P3 helpers stay generic
  (list/filter/tabs/modal), game meaning stays in scripts.

## Done When

- `minimal_3d` scene+HUD + `ui_showcase` script rewritten against new defaults
  with >=40% fewer authored lines and identical headless behavior
  (`DEMI_HEADLESS=1 demi run --max-frames N`, layout goldens, replay tests).
- `demi validate` passes old verbose files and new terse files with zero new
  errors; `--resolved` output explains every defaulted field.
- `scripts/stubs/demi.lua` + docs + `tests/` updated in the same change per
  slice (per AGENTS.md complete-slice rule); full `ctest --preset linux-debug`
  green.
