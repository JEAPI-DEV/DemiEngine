# Input Actions

Use `Input.down`, `pressed`, `released`, `value` and `source` for named actions.
`Input.vector` clamps a two-axis action to unit length; `Input.raw_vector` returns
the resolved action values without an additional normalization step. A binding's
own normalization setting still applies. Both return two numbers, `x, y`.

Raw keyboard queries are explicitly named `key_down`, `key_pressed` and
`key_released`. They bypass action bindings and are mainly useful for tools or
text-entry shortcuts. The former `is_*` and `action_*` names have been removed;
there are no compatibility aliases.

Gameplay code should depend on player intent and let the action system map
physical devices to actions. Actions are declared under `input.actions` and can
combine keyboard, mouse, gamepad, touch-driven virtual controls, and multiple
local players:

```json
{
  "input": {
    "actions": {
      "move": {
        "type": "vector2",
        "context": "gameplay",
        "bindings": [
          {"input": "key:a", "vector": [-1, 0]},
          {"input": "key:d", "vector": [1, 0]},
          {"input": "key:w", "vector": [0, -1]},
          {"input": "key:s", "vector": [0, 1]},
          {"input": "gamepad:stick:left", "deadzone": 0.18},
          {"input": "virtual:move"}
        ]
      },
      "fire": {
        "type": "button",
        "bindings": [
          {"input": "mouse:left"},
          {"input": "gamepad:south"},
          {"input": "virtual:fire"}
        ]
      }
    }
  }
}
```

Explicit actions require `type` and object-form `bindings`, as shown above.
Omitted `context` uses the canonical `"gameplay"` default. Specify it only when
an action belongs to another context, such as `"menu"`; existing explicit
gameplay contexts remain valid. Empty or non-string contexts are invalid.
Legacy string-array bindings are not supported. Inputs
and action names are case-insensitive. Supported device names include
`key:<key>`, `mouse:<button>`, `gamepad:<button>`,
`gamepad:axis:<axis>`, `gamepad:stick:left`, `gamepad:stick:right`, and
`virtual:<control>`. One-dimensional actions can select a virtual stick
component with `virtual:<control>:x` or `virtual:<control>:y`.

Bindings can specify `scale`, `vector`, `deadzone`, `invert`, `normalize`, and
`player`. Actions can specify `button`, `axis1d`, or `vector2`, an input
`context`, and an optional local `player`.

Lua exposes held, pressed, released, scalar, vector, and source state:

```lua
local Input = require("demi.input")

local x, y = Input.raw_vector("move", 1)
if Input.pressed("fire", 1) then
  shoot()
end
```

Contexts can be enabled or disabled for gameplay, menu, vehicle, or chat
screens. `Input.assign_gamepad(device, player)` assigns devices independently.
`Input.rebind(...)`, `Input.save_bindings(...)`, and
`Input.load_bindings(...)` support one-based runtime binding selection; relative
files are stored below `Application.user_data_path()`, including on Android.
Absolute binding paths are used as supplied. Relative binding paths are rejected
until user-data storage is configured, so early calls cannot access the working
directory. The runtime's Lua-free
`GameplayInputService` owns the mutable binding map, context selection, key
queries, and gamepad assignments. Lua bindings adapt that service directly.
Initialization selects the `gameplay` context; loading project scripts replaces
the bindings with the project's authored map while retaining selected contexts.
An empty enabled-context set disables all actions. Disabling the last context
therefore leaves named action queries neutral until a context is enabled again;
raw key queries are unaffected. Native resolver callers may pass a null context
filter for unfiltered queries.

Recorded replay actions obey the same explicit context filter as live input.
Their context comes from the current authored action definition because replay
frames contain resolved values, not context metadata. An enabled recorded action
still takes precedence over physical bindings and retains its recorded player
filter. Recorded actions without an authored definition are neutral in filtered
queries, but remain available to unfiltered native replay queries.

`Input.touches()` returns stable IDs, phases, position, delta, and pressure.
`Input.gestures()` recognizes tap, double-tap, long-press, drag, pinch, and
rotate. HUD nodes of type `virtual_button` or `virtual_stick` publish the
authored `control` as a `virtual:<control>` binding. UI capture is tracked per
touch ID.

`Application.safe_area()`, `Application.ui_scale()`, and the other Application
APIs expose display, lifecycle, clipboard, keyboard, and writable storage
services, so Lua code needs no platform-specific branches.
