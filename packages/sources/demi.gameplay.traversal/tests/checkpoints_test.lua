local Events = require("demi.gameplay.events")
local Checkpoints = require("demi.gameplay.checkpoints")

Test.case("checkpoint round trips stable entrance data", function()
  local events = Events.new(); local first = Checkpoints.new(events)
  first:set({ id = "cp-2", scene = "scene://cave", entrance = "east", version = 3 })
  local second = Checkpoints.new(events)
  Test.equal(second:load(first:save()), true); Test.equal(second.current.entrance, "east")
end)

Test.case("respawn without checkpoint is rejected", function()
  Test.equal(Checkpoints.new(Events.new()):respawn(), false)
end)

Test.case("scene entrance request requires stable identifiers", function()
  local events, requested = Events.new(), nil
  events:on("scene_entrance_requested", function(value) requested = value end)
  local checkpoints = Checkpoints.new(events)
  Test.equal(checkpoints:enter({ scene = "scene://town" }), false)
  Test.equal(checkpoints:enter({ scene = "scene://town", entrance = "north" }), true)
  events:flush()
  Test.equal(requested.entrance, "north")
end)
