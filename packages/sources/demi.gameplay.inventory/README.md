# Inventory and equipment

Package: `demi.gameplay.inventory`.

Stores one count per item ID and maps equipment slots to owned items. Capacity
limits distinct item IDs, not total items. Exports `demi.gameplay.inventory`;
depends on `demi.gameplay.events`. See [package installation](../../README.md).

```lua
local Events = require("demi.gameplay.events")
local Inventory = require("demi.gameplay.inventory")

local events = Events.new()
local inventory = Inventory.new(events, 20)
assert(inventory:add("potion", 8, 5) == 5)
inventory:add("sword", 1, 1)
assert(inventory:equip("hand", "sword"))
events:flush()
local saved = inventory:save()
local restored = Inventory.new(events)
assert(restored:load(saved))
```

`add(item, count, maximum)` takes a per-item cap on each call; the cap is not
stored as an item definition. `remove(item, count)` returns the amount removed.
`equip(slot, nil)` clears a slot. Removing the last item does not automatically
unequip it, so the game must enforce that rule if needed. Invalid or nonfinite counts transfer nothing. A cap below the current count also
adds nothing and never removes existing items.

The caller owns and flushes the event bus. Successful additions/removals queue
`inventory_changed` with the resulting count; accepted equip calls queue
`equipment_changed`. State changes before dispatch. Keep the unsubscribe
function returned by `events:on` and call it when its owner is destroyed.
`load` emits no events.

`save` returns a Lua table, not a disk save or independent snapshot: its
`stacks` and `equipment` tables are shared with the inventory. `load` also
retains the supplied tables and only checks that the outer value and `stacks`
are tables. The game must validate loaded content and serialize/copy it as
needed. This package does not load item assets, enforce equipment types, apply
item effects, or provide inventory UI.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.inventory
```

## Count queries and transfers (1.2)

`count(item)` returns the current quantity or zero. `transfer_to(destination,
item, count, maximum)` moves only what the destination can accept, respecting its
item capacity and optional per-item cap. It returns the amount moved. Rejected
transfers and self-transfers return zero without removing anything. The caller
still owns and flushes event buses; observers see both inventories updated.

```lua
local cargo = Inventory.new(events, 1)
local moved = inventory:transfer_to(cargo, "metal", 4, 4)
print(inventory:count("metal"), cargo:count("metal"), moved)
```

Reservation and hauling policy belongs to the game; inventory transfers do not
teleport entities or choose carriers.
