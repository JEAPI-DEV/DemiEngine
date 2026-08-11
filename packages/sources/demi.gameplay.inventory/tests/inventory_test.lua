local Events = require("demi.gameplay.events")
local Inventory = require("demi.gameplay.inventory")

Test.case("capacity and stacks clamp", function()
  local inventory = Inventory.new(Events.new(), 1)
  Test.equal(inventory:add("coin", 8, 5), 5); Test.equal(inventory:add("key", 1), 0)
  Test.equal(inventory:remove("coin", 99), 5); Test.equal(inventory.stacks.coin, nil)
end)

Test.case("save load keeps equipment", function()
  local first = Inventory.new(Events.new()); first:add("sword"); first:equip("hand", "sword")
  local second = Inventory.new(Events.new()); Test.equal(second:load(first:save()), true)
  Test.equal(second.equipment.hand, "sword")
end)
