# Runtime UI

Tree UI uses the existing `*.hud.json` format with a top-level `root` node.
A tree node owns its content, layout, state, and children. Renderers consume
the resulting resolved rectangles.

Every node type can own `children`. A `panel` can therefore provide a visible
background, padding, and layout for a complete UI section. Child positions and
anchors are resolved against the parent's padded content rectangle, so moving,
hiding, disabling, or anchoring a panel applies naturally to its subtree.

All nodes support `anchor_min` / `anchor_max`, `margin`, `padding`, `min_size`,
and `max_size`. Nodes with children can additionally use `row`, `column`, or
`grid` layout with `alignment`, `gap`, and `columns`. Sizes are authored in
`canvas_size` units and the renderer scales the resolved canvas to the viewport.
The layout engine itself can also resolve directly at arbitrary viewport sizes
for headless tests.

For example, this panel fills its parent while respecting a 16-unit inset, and
its button stays at the bottom-right of the panel's padded content:

```json
{
  "id": "menu_panel",
  "type": "panel",
  "anchor_min": [0, 0],
  "anchor_max": [1, 1],
  "margin": 16,
  "padding": 24,
  "children": [
    {
      "id": "confirm",
      "type": "button",
      "anchor_min": [1, 1],
      "anchor_max": [1, 1],
      "position": [-180, -48],
      "size": [180, 48]
    }
  ]
}
```

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

Runtime-generated UI uses the same retained tree and layout path as authored
nodes. `Hud.create(parent, definition)` returns a generation-checked handle;
`Hud.clone`, `Hud.remove`, `Hud.reparent`, and `Hud.clear_children` reject stale
handles and apply structural changes transactionally. Removing a subtree also
clears focus and pointer captures that refer to it. This is intentionally
generic: projects decide whether a generated button represents an inventory
item, player, save slot, setting, dialogue choice, or debug command.

For large collections, `Hud.visible_range(item_count, item_extent,
scroll_offset, viewport_extent, overscan)` returns the bounded logical range a
game should represent with live nodes. Ten thousand data rows therefore do not
require ten thousand retained controls. `examples/ui_showcase` demonstrates
runtime row creation without a genre-specific engine widget.

Text nodes support `text_wrap` (`none`, `word`, or `grapheme`),
`text_alignment`, `text_vertical_alignment`, `line_spacing`, `max_lines`, and
`text_overflow` (`visible`, `clip`, or `ellipsis`). The backend-neutral
`Text.layout` API exposes the same line decisions to game code and headless
tests. `Text.grapheme_count` and `Text.grapheme_slice` use Unicode grapheme
boundaries rather than bytes, while `Text.parse_rich` accepts only the
documented non-executable `[color]`, `[em]`, `[strong]`, `[link]`, and `[icon]`
tags.

HUD files may provide a `locales` object keyed by locale. `Hud.set_locale`
reapplies every node's localization key and preserves the last valid locale on
failure; `Hud.set_pseudo_locale` helps expose expansion and clipping bugs.
`Hud.tween` animates `opacity`, `x`, `y`, or `scale` using node handles. Tweens
are cancelled when their target is removed, and `Hud.set_reduced_motion(true)`
finishes presentation changes immediately.

Control actions continue through `---@handle_action` and the `hud_action`
event. No Lua code is required to position or resize widgets.

For searchable lists, `Hud.get_text` returns a tree text-input's current value.
`Regex.matches(value, pattern, case_sensitive)` performs an ECMAScript regular
expression search (`case_sensitive` defaults to `false`), and
`Regex.is_valid(pattern)` lets scripts handle incomplete expressions while the
user is typing. The showcase filters inventory item visibility only when the
search pattern changes.
