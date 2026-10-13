local Test = require("demi.test")
local Needs = require("demi.gameplay.needs")

Test.case("needs drain, clamp and report actual changes", function()
  local needs = Needs.new({ stamina = { value = 80 }, food = { value = 60 } })
  needs:update(10, { stamina = -2, food = -1 })
  Test.equal(needs:get("stamina"), 60)
  Test.equal(needs:get("food"), 50)
  Test.equal(needs:change("stamina", -100), -60)
  Test.equal(needs:change("stamina", 120), 100)
end)

Test.case("separate low and recovery thresholds prevent immediate task churn", function()
  local needs = Needs.new({ oxygen = { value = 25, low = 25, recovered = 95 } })
  Test.truthy(needs:urgent())
  needs:change("oxygen", 1)
  Test.equal(needs:urgent(), false)
  Test.equal(needs:recovered(), false)
  needs:update(10, { oxygen = 10 })
  Test.truthy(needs:recovered())
end)

Test.case("instances and caller definitions are independent", function()
  local definitions = { stamina = { value = 50 } }
  local first, second = Needs.new(definitions), Needs.new(definitions)
  definitions.stamina.value = 0
  first:change("stamina", -30)
  Test.equal(second:get("stamina"), 50)
  Test.equal(first:get("stamina"), 20)
end)

Test.case("invalid updates reject without partial mutation", function()
  local needs = Needs.new({ stamina = { value = 50 }, oxygen = { value = 50 } })
  Test.equal(
    pcall(function()
      needs:update(1, { stamina = -1, oxygen = math.huge })
    end),
    false
  )
  Test.equal(needs:get("stamina"), 50)
  Test.equal(needs:get("oxygen"), 50)
  Test.equal(
    pcall(function()
      needs:update(-1, {})
    end),
    false
  )
  Test.equal(
    pcall(function()
      needs:change("unknown", 1)
    end),
    false
  )
  Test.equal(
    pcall(function()
      Needs.new({ stamina = { low = 90, recovered = 50 } })
    end),
    false
  )
end)
