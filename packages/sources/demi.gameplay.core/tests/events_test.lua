local Events = require("demi.gameplay.events")

Test.case("priority and registration order are deterministic", function()
  local bus, order = Events.new(), {}
  bus:on("hit", function() table.insert(order, "late") end, 0)
  bus:on("hit", function() table.insert(order, "first") end, 10)
  bus:emit("hit", {})
  bus:flush()
  Test.equal(table.concat(order, ","), "first,late")
end)

Test.case("unsubscribe during dispatch is safe", function()
  local bus, calls, stop = Events.new(), 0
  stop = bus:on("tick", function() calls = calls + 1; stop() end)
  bus:emit("tick"); bus:emit("tick"); bus:flush()
  Test.equal(calls, 1)
end)
