# Input Actions

Gameplay code should depend on player intent rather than physical devices.
Actions are declared under `input.actions` and can combine keyboard, mouse,
gamepad, touch-driven virtual controls, and multiple local players:

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
          "mouse:left",
          "gamepad:south",
          "virtual:fire"
        ]
      }
    }
  }
}
```

Legacy string arrays and scaled one-dimensional bindings remain valid. Inputs
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
local x, y = Input.action_vector("move", 1)
if Input.action_pressed("fire", 1) then
  shoot()
end
```

Contexts can be enabled or disabled for gameplay, menu, vehicle, or chat
screens. `Input.assign_gamepad(device, player)` assigns devices independently.
`Input.rebind(...)`, `Input.save_bindings(...)`, and
`Input.load_bindings(...)` support one-based runtime binding selection; relative
files are stored below `Application.user_data_path()`, including on Android.

`Input.touches()` returns stable IDs, phases, position, delta, and pressure.
`Input.gestures()` recognizes tap, double-tap, long-press, drag, pinch, and
rotate. HUD nodes of type `virtual_button` or `virtual_stick` publish the
authored `control` as a `virtual:<control>` binding. UI capture is tracked per
touch ID.

`Application.safe_area()`, `Application.ui_scale()`, and the other Application
APIs expose display, lifecycle, clipboard, keyboard, and writable storage
services without platform-specific Lua branches.
