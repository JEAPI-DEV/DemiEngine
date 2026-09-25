# Checkpoints and respawn

Package: `demi.gameplay.checkpoints`.

Stores the current checkpoint and queues respawn or scene-entrance requests.
Exports `demi.gameplay.checkpoints`; depends on `demi.gameplay.events`.
This checkout uses the renamed package; install from the local `packages`
registry as described in [package installation](../../README.md). Renaming a
local source does not publish the new name to the hosted registry.

```lua
local Events = require("demi.gameplay.events")
local Checkpoints = require("demi.gameplay.checkpoints")

local events = Events.new()
local checkpoints = Checkpoints.new(events)
local requested
local unsubscribe = events:on("respawn_requested", function(value)
  requested = value
end)
checkpoints:set({
  id = "gate", scene = "scene://game/cave", entrance = "east", version = 1,
})
assert(checkpoints:respawn())
events:flush()
assert(requested.entrance == "east")
local saved = checkpoints:save()
unsubscribe()
```

`set` requires string `id` and `scene`, and retains optional `entrance`,
`prefab`, `data`, and `version` (default 1). It queues `checkpoint_changed`.
`respawn` returns `false` without a checkpoint; otherwise it queues
`respawn_requested`. `enter({ scene = ..., entrance = ..., data = ... })`
requires string scene/entrance values and queues `scene_entrance_requested`
without changing the current checkpoint.

The caller owns the event bus, flushes it at a chosen gameplay boundary, and
unsubscribes listeners when their owner is destroyed. Listeners implement
scene preparation, activation, positioning, and respawn rules. The package
does not perform a transition or resolve scene, entrance, or prefab references.

`save` returns `nil` or a shallow copy of the checkpoint table; nested `data`
remains shared. It does not write a file. `load` accepts a table with string
`id` and `scene`, retains that table directly, and emits no event. It neither
validates the remaining fields nor migrates versions. The game owns durable
save formatting, validation, migrations, and disk I/O.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.checkpoints
```
