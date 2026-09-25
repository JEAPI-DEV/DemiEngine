# Interactions and pickups

Package: `demi.gameplay.interactions`.

Selects an interaction from game-supplied candidates and tracks single-use
pickups. Exports `demi.gameplay.interactions` and `demi.gameplay.pickups`;
depends on `demi.gameplay.events`. See [package installation](../../README.md).

```lua
local Events = require("demi.gameplay.events")
local Interactions = require("demi.gameplay.interactions")
local Pickups = require("demi.gameplay.pickups")

local events = Events.new()
local interactions = Interactions.new(events)
local pickups = Pickups.new(events)
pickups:add("coin-1", { item = "coin", count = 3 })
interactions:set("coin-1", { distance = 1, prompt = "Collect" })
local unsubscribe = events:on("interaction_selected", function(value)
  if pickups:collect(value.entity, "player") then
    interactions:remove(value.entity)
  end
end)
assert(interactions:confirm(2))
events:flush()
assert(not pickups:collect("coin-1", "player"))
unsubscribe()
```

`best(max_distance)` filters disabled/out-of-range candidates, then chooses
highest priority, shortest distance, and lexicographically smallest ID.
Update candidates with `set`; it replaces their fields, so supply all values
you need to retain. `confirm` queues `interaction_selected` and returns
`true, id`, or `false` when no candidate qualifies. Confirmation does not remove
the candidate, so repeated confirmations can queue repeated events.

`collect(id, collector)` removes an enabled pickup immediately and queues
`pickup_collected` with `pickup`, `collector`, `item`, `count`, and `payload`.
Later collections fail unless the game adds that pickup again. Collection does
not check distance or inventory capacity.

The game owns the bus and chooses when to flush. Events emitted by listeners
are drained by the same flush; in this example that includes `pickup_collected`.
Subscribe to it to grant items or update presentation, and unsubscribe when
the listener owner is destroyed. Neither module reads input, queries physics,
draws prompts, modifies inventory, or destroys scene entities. The game supplies
distance/enabled state and handles those effects.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.interactions
```
