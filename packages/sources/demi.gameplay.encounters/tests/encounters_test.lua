local Events = require("demi.gameplay.events")
local Encounters = require("demi.gameplay.encounters")

Test.case("spawn failures are explicit and stable", function()
  local events, failed = Events.new(), nil
  events:on("spawn_failed", function(value) failed = value.reason end)
  local encounter = Encounters.new(events, function() return nil, "path_unavailable" end)
  encounter:configure({ { spawns = { { at = 0, prefab = "raider" } } } })
  encounter:start_next(); encounter:update(0); events:flush()
  Test.equal(failed, "path_unavailable"); Test.equal(encounter:ready_for_next(), true)
end)

Test.case("objective fails once", function()
  local encounter = Encounters.new(Events.new(), function() end)
  Test.equal(encounter:fail("keep_destroyed"), true); Test.equal(encounter:fail("again"), false)
end)
