# Health and damage

Package: `demi.gameplay.health`.

Tracks health by string entity ID, clamps damage/healing, and reports defeat
when health crosses from positive to zero. Exports `demi.gameplay.health`;
depends on `demi.gameplay.events`.

After [installing the package](../../README.md), create one service for the
entities whose health you want to manage:

```lua
local Events = require("demi.gameplay.events")
local Health = require("demi.gameplay.health")

local events = Events.new()
local health = Health.new(events, { policy = Health.team_policy() })
health:add("player", 100, { team = "blue" })
health:add("enemy", 20, { team = "red" })
local defeated
local unsubscribe = events:on("entity_defeated", function(value)
  defeated = value.entity
end)
health:damage({ source = "player", target = "enemy", amount = 25 })
events:flush()
assert(defeated == "enemy")
unsubscribe()
```

`damage` returns success and the amount applied, or `false` and a reason such
as `missing_target`, `invulnerable`, or `blocked`. `heal` caps health at its
maximum. Use `set_invulnerable` for an explicit flag; there is no timer.
`get` returns the live entry, and `remove` forgets it without destroying an entity.

A custom `policy(request, target, amount, service)` returns the allowed damage.
`team_policy` blocks damage between registered members of the same team unless
`friendly_fire = true`; `self_damage = false` also blocks self hits.

The caller owns the bus and flush timing. Mutations happen immediately;
`damage_applied`, `health_changed`, and `entity_defeated` are queued. Flush after
the gameplay step and unsubscribe listeners when their owner is destroyed.
Defeat can happen again after healing above zero. This service does not listen
for `damage_requested` automatically: connect that event to `health:damage`
in game code. Scoring, loot, animation, persistence, and entity destruction
remain the game's responsibility.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.health
```
