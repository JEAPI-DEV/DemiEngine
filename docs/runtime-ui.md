# Runtime UI

Tree UI uses the existing `*.hud.json` format with a top-level `root` node.
Legacy `elements` HUDs remain supported. A tree node owns its content, layout,
state, and children; renderers only receive resolved rectangles through the
temporary legacy projection adapter.

Containers support `row`, `column`, and `grid` layout, `anchor_min` /
`anchor_max`, `margin`, `padding`, `min_size`, `max_size`, `alignment`, `gap`,
and `columns`. Sizes are authored in `canvas_size` units and the renderer scales
the resolved canvas to the viewport. The layout engine itself can also resolve
directly at arbitrary viewport sizes for headless tests.

Supported node types are `container`, `panel`, `label`, `text`, `image`,
`button`, `toggle`, `slider`, `text_input`, `scroll`, `list`, `progress`, and
`modal`. Interactive controls accept `action`, `disabled`, `focusable`, and
`accessibility_label`. Tab, arrow keys, controller D-pad, pointer input, and the
controller accept button share the same focus state. A visible modal traps
focus inside its subtree.

Set `theme` and `localization_file` to paths relative to the HUD file. Theme
files provide `styles` and an optional `action_map`; localization files provide
a `localization` object. Values declared directly in the HUD take precedence.
See `examples/ui_showcase` for a menu/settings/inventory document.

`action_effects` can attach declarative `show`, `hide`, and `focus` changes to
an action. Use these for screen/tab transitions that do not require gameplay
logic. Actions are still emitted to Lua after the built-in state change.

Lua changes state without controlling layout:

- `Hud.set_text(id, text)` and `Hud.set_button_label(id, text)`
- `Hud.set_value(id, value)`
- `Hud.set_checked(id, checked)`
- `Hud.set_disabled(id, disabled)` and `Hud.set_visible(id, visible)`
- `Hud.focus_next(reverse)` and `Hud.focused()`

Control actions continue through `---@handle_action` and the `hud_action`
event. No Lua code is required to position or resize widgets.

For searchable lists, `Hud.get_text` returns a tree text-input's current value.
`Regex.matches(value, pattern, case_sensitive)` performs an ECMAScript regular
expression search (`case_sensitive` defaults to `false`), and
`Regex.is_valid(pattern)` lets scripts handle incomplete expressions while the
user is typing. The showcase filters inventory item visibility only when the
search pattern changes.
