# Typed Lua Script Properties

## Editor component annotations

Game-specific behavior can appear in the editor's Add Component selector
without becoming a native C++ component:

```lua
---@demi_component
---@display_name Player Controller
---@category Gameplay
---@description Moves and rotates the player.
local Player = {}

---@demi_property
---@label Move Speed
---@range 0 20
Player.speed = 6.0

---@demi_property
---@options red, blue
Player.team = "red"
```

`@demi_component` opts the file into discovery. Display name defaults to the
humanized filename and category defaults to `Scripting`, so the other component
annotations are optional. The module's existing `script://` URI is its stable
identity; developers do not author a second ID or a format version.

`@demi_property` exposes the following table-field assignment. The editor and
runtime infer its type and default from the assigned Boolean, number, string,
or numeric table. Use `@demi_property asset`, `entity`, `integer`, or another
supported type when inference is ambiguous. Optional annotations include:

- `@label Friendly Name`
- `@description Tooltip text`
- `@range minimum maximum`
- `@options first, second, third` for enums
- LuaLS `@type` immediately after `@demi_property`

The resulting Inspector section uses the friendly component header and typed
controls. Adding it still authors the standard `LuaScript` scene component;
the annotation schema also validates and installs defaults at runtime. An
entity currently supports one Lua script, so further Lua component choices are
disabled with an explanation.

Native components already attached to an entity are omitted from Add Component.
For example, `Buildable` is available on other entities but is not listed for
an entity that already contains it.

## Legacy property schemas

Scene and prefab authors can configure a `LuaScript` without changing its Lua
module. A module without `@demi_component` annotations can still opt into
validation by declaring `property_schema` on the returned script table:

```lua
local Player = {}

---@type ScriptPropertySchema
Player.property_schema = {
  speed = {
    type = "number",
    default = 6.0,
    minimum = 0.0,
    maximum = 20.0,
  },
  team = {
    type = "enum",
    values = { "red", "blue" },
    default = "red",
  },
  model = { type = "asset", required = true },
}

function Player:on_start()
  Debug.log("speed=" .. tostring(self.speed))
end

return Player
```

The owning scene or prefab supplies only overrides:

```json
{
  "LuaScript": {
    "module": "script://scripts/player.lua",
    "properties": {
      "speed": 8.5,
      "model": "asset://characters/player"
    }
  }
}
```

Defaults are installed before `on_create`. Authored values replace defaults.
Do not assign a configurable property again in `on_create`, because that would
replace the authored value.

Supported types are `boolean`, `number`, `integer`, `string`, `asset`,
`entity`, `enum`, `array`, `object`, `vec2`, `vec3`, and `color`. Number and
integer definitions may declare `minimum` and `maximum`. Enum definitions must
declare a non-empty string `values` array. Asset values must use `asset://`
IDs. A required property without a default must be authored.

When a schema is present, unknown properties, wrong types, invalid enum values,
and out-of-range numbers reject script activation. A rejected hot reload keeps
the previous script instance running. Modules without `property_schema` retain
the legacy behavior and accept arbitrary JSON properties.
