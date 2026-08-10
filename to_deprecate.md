# DemiEngine Deprecation Ledger

Last audited: 2026-08-10

## Active deprecations

None. This repository intentionally exposes one canonical path for each item
retired below. Do not restore forwarding aliases in the runtime. If external
projects need migration support, provide an offline source-data migration tool
instead of adding another runtime representation.

## Retired in the canonical-API migration

| Removed surface | Canonical replacement |
| --- | --- |
| `on_ui_hover` and `on_ui_click` | `on_ui_event` or a typed `on_ui_<type>` callback |
| `hud_action` | typed `ui_event`/`ui_<type>` channels or `---@handle_action` |
| `Hud.text` and `Hud.rect` | authored HUD nodes or `Hud.create`/`Hud.clone` |
| `Hud.set_button_label` | `Hud.set_text` |
| `Hud.set_text_scale` | `Hud.set_font_size` |
| mixed-responsibility `Runtime` Lua service | `Application` and `Physics` |
| shorthand input-action documents | explicit typed actions, contexts, and qualified object bindings |
| `Input.axis` and `Input.vector` | `Input.action_value` and `Input.action_vector` |
| numeric 3D animation clips | `clip_name` and `model_clip_name` |
| `root_motion_per_second` | sampled `root_motion_track` data |
| `Entity.set_sprite_color` | `Sprite2D.set_color` |
| CLI `--frames` | `--max-frames` |
| incomplete asset-manifest reimport guessing | complete versioned manifests |
| arbitrary `json-data` routing | subsystem-owned typed JSON importers |
| P3/P6 PPM runtime images | alpha-capable packaged image formats such as PNG |
| texture `color_key` | source-authored alpha |
| panel/control `color` fill fallback | `background_color` |
| HUD `group` and `Hud.set_group_visible` | structural parent nodes and `Hud.set_visible` |
| `UiDocument.pointerCaptureId` | per-pointer capture state |

All checked-in examples, templates, schemas, Lua stubs, and focused tests use
the replacements. Parsers and bindings reject or omit the removed surfaces so
they cannot silently return.

## Already removed or rejected

- raylib/rlgl renderers, resource types, and filesystem bridge;
- dual raylib/bgfx runtime selection;
- shader `platform_sources` and `platform_fallbacks`; use one bgfx `.sc`
  source set and cooked backend binaries;
- runtime platform-specific game shader source selection;
- editor-only durable state absent from versioned project data.

## Intentionally supported alternatives

These have distinct jobs and are not legacy aliases:

- Vulkan, OpenGL, OpenGL ES, Automatic, and Noop `GraphicsDevice` selection;
- material and shader fallbacks used for capability-safe rendering;
- fallback fonts and missing-glyph handling;
- explicit save migrations and versioned offline source-data migrations;
- low-level input inspection for rebinding, tools, diagnostics, and
  pointer-driven gameplay;
- typed component bindings alongside reflective `Entity.get`/`Entity.set`;
- headless/Noop rendering for tests and dedicated servers.

## Rule for future candidates

Record a future candidate here only after naming its canonical replacement and
all remaining consumers. Migrate examples and templates first. Once the
compatibility policy permits removal, delete the parser branch, binding,
storage, tests, and obsolete documentation together, and add a regression test
that prevents the old surface from returning.
