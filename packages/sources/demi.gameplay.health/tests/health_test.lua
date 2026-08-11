local Events = require("demi.gameplay.events")
local Health = require("demi.gameplay.health")

Test.case("lethal hits clamp and defeat once", function()
  local events, defeated = Events.new(), 0
  events:on("entity_defeated", function() defeated = defeated + 1 end)
  local health = Health.new(events); health:add("enemy", 10)
  health:damage({ target = "enemy", amount = 50 }); health:damage({ target = "enemy", amount = 1 })
  events:flush()
  Test.equal(health:get("enemy").current, 0); Test.equal(defeated, 1)
end)

Test.case("invulnerability and healing clamp", function()
  local events, health = Events.new(), nil
  health = Health.new(events); health:add("player", 10, { current = 4, invulnerable = true })
  local applied = health:damage({ target = "player", amount = 2 })
  Test.equal(applied, false)
  health:heal("player", 100); Test.equal(health:get("player").current, 10)
end)

Test.case("target removal during event dispatch is safe", function()
  local events, health = Events.new(), nil
  health = Health.new(events); health:add("enemy", 1)
  events:on("damage_applied", function(payload) health:remove(payload.target) end, 10)
  health:damage({ target = "enemy", amount = 1 }); events:flush()
  Test.equal(health:get("enemy"), nil)
end)

Test.case("team policy blocks friendly fire but permits enemies", function()
  local events = Events.new()
  local health = Health.new(events, { policy = Health.team_policy() })
  health:add("ally", 10, { team = "blue" })
  health:add("friend", 10, { team = "blue" })
  health:add("enemy", 10, { team = "red" })
  Test.equal(health:damage({ source = "ally", target = "friend", amount = 2 }), false)
  Test.equal(health:damage({ source = "ally", target = "enemy", amount = 2 }), true)
  Test.equal(health:get("friend").current, 10)
  Test.equal(health:get("enemy").current, 8)
end)
