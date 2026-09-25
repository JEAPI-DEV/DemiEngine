# Gameplay events

`demi.gameplay.events` connects Lua gameplay systems through named, queued events.
Each queue belongs to its caller. Create one per scene, game, or subsystem; there
is no global event bus and no dependency on physics or rendering.

## Use

Install from this checkout's development registry:

```sh
demi package add demi.gameplay.events@1.0.0 --registry /path/to/DemiEngine/packages
```

```lua
local Events = require("demi.gameplay.events")
local events = Events.new()

local unsubscribe = events:on("door_opened", function(payload)
    print("Opened " .. payload.id)
end)

events:emit("door_opened", {id = "front_door"})
events:flush()
unsubscribe()
```

`emit(name, payload)` queues an event; it does not call listeners immediately.
`flush()` dispatches queued events in emission order. Higher listener priorities
run first (the optional third argument to `on`); equal priorities retain
registration order. `on` returns an unsubscribe function.

Events emitted by a listener are processed during the same flush. Avoid cycles
that keep emitting indefinitely. Payloads are Lua references, not snapshots;
mutating a table before flushing changes what listeners receive. Callback errors
propagate to the caller. This is a local gameplay queue, not a network transport
or persistent event log.

## Development

This package replaces the development-era name `demi.gameplay.core`. There is no
alias package. The Lua import remains `demi.gameplay.events`. Reinstall dependent
packages from the local registry to refresh their manifests and lock hashes.

Run `demi package test packages/sources/demi.gameplay.events` from the engine root.
