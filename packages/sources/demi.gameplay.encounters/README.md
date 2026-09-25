# Encounter waves

Package: `demi.gameplay.encounters`.

Schedules spawn callbacks for waves, tracks live IDs, and reports completion
or an explicit objective failure. Exports `demi.gameplay.encounters`; depends
on `demi.gameplay.events`. See [package installation](../../README.md).

This minimal example uses an in-memory spawn callback. Replace it with game
code that creates an entity and returns its unique ID, or `nil, reason`:

```lua
local Events = require("demi.gameplay.events")
local Encounters = require("demi.gameplay.encounters")

local events = Events.new()
local encounter = Encounters.new(events, function(request)
  return "enemy-" .. request.order
end)
encounter:configure({
  { spawns = { { at = 0, prefab = "prefab://game/raider", entrance = "east" } } },
})
assert(encounter:start_next())
encounter:update(0)
events:flush()
encounter:defeated("enemy-1")
if encounter:ready_for_next() then
  encounter:start_next()
end
events:flush()
assert(encounter.completed)
```

`update(elapsed)` takes elapsed time since the current wave started, not a
frame delta. The game owns that clock and resets it for each wave. Due requests
run in reverse definition order, not sorted timestamp order. Each request is
removed after one callback attempt; failures are not retried automatically.

The caller owns and flushes the bus. `wave_started`, `spawn_requested`,
`spawn_failed`, `objective_failed`, and `encounter_completed` are queued.
Despite its name, `spawn_requested` is emitted after the spawn callback returns
an ID. Keep listener unsubscribe functions and call them on owner teardown.
Connect game defeat notifications to `defeated(id)` yourself.

`ready_for_next` checks that both pending requests and live IDs are empty.
`start_next` does not enforce that check: the game must guard advancement.
Calling it beyond the last wave emits completion and returns `false`.
`fail(reason)` emits failure once and blocks further `start_next` calls, but
does not cancel pending requests or stop `update`; stop updating after failure
if that is the desired rule. `configure` only replaces wave definitions, so
create a new instance for a fresh encounter.

The package does not evaluate objectives, spawn engine entities itself, perform
navigation, or assign rewards. Spawn success and timing depend on game code.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.encounters
```
