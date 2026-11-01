local Test = require("tests.ui_test")
local Session = require("dungeon.session")
local Inventory = require("dungeon.inventory")

return {
  name = "Inventory can change weapons while a swing is paused",
  func = function()
    Test.expect_scene("scene://dungeon/floor1")
    Test.wait(0.1)
    Test.reset()
    Session.set_driver(function()
      return { attack = true, aim_x = 10, aim_y = 5.55 }
    end)
    Test.wait(0.02)
    Test.touch("inventory_button")
    Session.set_driver(nil)
    local frozen = Session.observe()
    Test.expect(frozen.player.melee ~= nil, "Probe did not open the bag during a swing")
    Test.wait(0.2)
    Test.expect(Session.observe().time == frozen.time, "Bag did not pause simulation")
    Test.touch("bag_slot_4")
    Test.touch("item_use")
    local changed = Session.observe()
    print(
      "Weapon swap probe: selected="
        .. Session.game.menus.selected
        .. " equipped="
        .. tostring(changed.player.inventory.equipment.weapon)
    )
    Test.expect(Inventory.stats(changed.player).weapon == "oak_staff", "Paused swing blocked equip")
    Test.expect(changed.player.melee == nil, "Old sword attack survived equipment change")
    Test.expect(
      changed.player.cooldown == frozen.player.cooldown,
      "Equipment change reset recovery"
    )
    Test.touch("bag_slot_1")
    Test.touch("item_use")
    Test.expect(
      Inventory.stats(Session.observe().player).weapon == "short_sword",
      "Second equip failed"
    )
    Test.touch("bag_close")
    Session.set_driver(function()
      return {}
    end)
    Test.wait(0.6)
    local resumed = Session.observe()
    Test.expect(resumed.time > frozen.time, "Simulation did not resume")
    Test.expect(resumed.player.melee == nil, "Cancelled swing resumed after closing the bag")
    Test.expect(resumed.player.cooldown == 0, "Retained recovery did not expire")
    Session.set_driver(nil)
  end,
}
