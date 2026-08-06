# Optional data-driven gameplay modules

These modules build common game state on top of the engine's read-only
`Data` asset API. They do not keep global state or render UI. Pass a plain Lua
state table to them and serialize that table with the normal save API.

```lua
local Flags = require("demi.data.flags")
local Inventory = require("demi.data.inventory")
local Dialogue = require("demi.data.dialogue")

local state = { flags = {}, inventory = {}, quests = {} }
Flags.set(state, "met_mira", true)
Inventory.add(state, "asset://items/iron_sword", 1)

local chapter = assert(Data.load("asset://story/chapter_01"))
local session = assert(Dialogue.start(chapter))
local node = assert(Dialogue.current(chapter, session, state))
```

Definitions and save state use stable IDs. If a content update renames an ID,
migrate saved IDs explicitly instead of guessing a replacement. The schemas in
`schemas/` are starting points that projects may copy and extend.
