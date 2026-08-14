# Typed Lua Script Properties

Scene and prefab authors can configure a `LuaScript` without changing its Lua
module. A module opts into validation by declaring `property_schema` on the
returned script table:

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
