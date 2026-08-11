local Events = require("demi.gameplay.events")
local Projectiles = require("demi.gameplay.projectiles")

Test.case("fast projectile uses the whole segment", function()
  local events, target = Events.new(), nil
  events:on("damage_requested", function(value) target = value.target end)
  local shots = Projectiles.new(events, function(x1, y1, x2)
    Test.truthy(x2 > x1 + 100); return { { entity = "enemy", fraction = 0.5 } }
  end)
  shots:spawn({ owner = "player", vx = 1000, damage = 2, life = 1 })
  shots:update(0.2); events:flush(); Test.equal(target, "enemy")
end)

Test.case("pool resets identity hits and double release", function()
  local events = Events.new()
  local shots = Projectiles.new(events, function() return {} end)
  local first = shots:spawn({ owner = "old", ignored = "old-ignore", pierce = 3 })
  first.hit_once.enemy = true
  Test.equal(shots:release(first), true); Test.equal(shots:release(first), false)
  local reused = shots:spawn({ owner = "new" })
  Test.equal(reused, first); Test.equal(reused.ignored, nil); Test.equal(reused.hit_once.enemy, nil)
end)

Test.case("trigger before solid does not consume projectile", function()
  local events, target = Events.new(), nil
  events:on("damage_requested", function(value) target = value.target end)
  local shots = Projectiles.new(events, function()
    return { { entity = "trigger", fraction = 0.1, trigger = true },
      { entity = "solid", fraction = 0.2 } }
  end)
  shots:spawn({ owner = "player", damage = 1 }); shots:update(0.1); events:flush()
  Test.equal(target, "solid")
end)

Test.case("hitscan sorts contacts and ignores triggers", function()
  local Hitscan = require("demi.gameplay.hitscan")
  local events, target = Events.new(), nil
  events:on("damage_requested", function(value) target = value.target end)
  local hit = Hitscan.fire(events, function()
    return { { entity = "far", fraction = 0.9 },
      { entity = "trigger", fraction = 0.1, trigger = true },
      { entity = "near", fraction = 0.2 } }
  end, { owner = "player", damage = 4 })
  events:flush()
  Test.equal(hit, "near")
  Test.equal(target, "near")
end)
