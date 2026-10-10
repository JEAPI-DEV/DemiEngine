# Character Needs

Named, bounded needs for survival games, NPC routines and colony simulations.
The module is independent of scenes, physics and UI. Higher values mean a need
is better satisfied: use nutrition rather than hunger, or stamina rather than fatigue.

```lua
local Needs = require("demi.gameplay.needs")

local needs = Needs.new({
  stamina = { value = 80, low = 25, recovered = 90 },
  nutrition = { value = 70, low = 30, recovered = 75 },
})

-- Rates are units per second. Only listed needs change.
needs:update(dt, { stamina = -0.4, nutrition = -0.15 })
if needs:urgent() then
  -- Ask the game's scheduler for a break.
end

-- After the game accepts a meal transaction:
local restored = needs:change("nutrition", 60)
```

`Needs.new(definitions)` copies named definitions. Defaults are maximum 100,
initial value equal to maximum, low threshold 25% and recovered threshold 90%.
Values clamp to `[0, maximum]`. Recovery must exceed the low threshold.
`get(name)` reads a value; `change(name, amount)` returns the actual applied change.
`update(dt, rates)` validates the entire request before changing any value.
Invalid names, nonfinite inputs and negative time are errors.

`urgent()` is true when any need is at or below its low threshold.
`recovered()` is true when every need has reached its recovery threshold.
Keep a break active until recovery: separate thresholds avoid switching tasks
back and forth near a single threshold. The game owns that task state.

The game also owns drain rates, food/water costs, habitat requirements, animation,
HUD presentation and consequences of depletion. No resource is consumed and no
entity is moved by this package. It does not implement save storage.

The Colony Builder example uses this package for its engineer's stamina,
nutrition and suit oxygen, while keeping habitat policy in `colony/needs.lua`.
Run `demi package test packages/sources/demi.gameplay.needs` for isolated tests.
