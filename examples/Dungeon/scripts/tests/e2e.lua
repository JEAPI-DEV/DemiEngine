local Test = require("tests.ui_test")
local Hud = require("dungeon.ui")
local Session = require("dungeon.session")
local Driver = require("tests.driver")
local Persistence = require("dungeon.persistence")
local Inventory = require("dungeon.inventory")
local Combat = require("dungeon.combat")
Persistence.slot = "hollow_crown_e2e"

return {
  tests = {
    {
      name = "Native collision, dodge, pause and restart",
      func = function()
        Test.expect_scene("scene://dungeon/floor1")
        Test.wait(0.1)
        Test.touch("start_button")
        Test.wait(0.1)
        Test.expect(Session.observe().phase == "playing", "Start button did not start gameplay")
        Session.set_driver(function()
          return { move_x = -1, dash = true }
        end)
        Test.wait(1.5)
        local player = Session.observe().player
        Test.expect(
          player.x >= 1.79 and player.x < 2.1,
          "Native wall collision did not stop movement/dash"
        )
        Test.expect(player.dash_cooldown > 0, "Dodge did not consume its cooldown")
        Session.set_driver(nil)
        Test.touch("pause_button")
        local before = Session.observe().time
        Test.wait(0.2)
        Test.expect(Session.observe().time == before, "Paused simulation advanced")
        Test.touch("resume_button")
        Test.wait(0.1)
        Test.expect(Session.observe().time > before, "Resume did not advance simulation")
        Session.set_driver(function()
          return {}
        end)
        for attempt = 1, 300 do
          if Session.observe().phase == "dead" then
            break
          end
          Test.wait(0.1)
        end
        Session.set_driver(nil)
        Test.expect(Session.observe().phase == "dead", "Enemy attacks did not produce a loss")
        Test.wait(0.15)
        Test.expect(
          Hud.get_text("overlay_title") == "THE HOLLOW CLAIMS YOU",
          "Defeat presentation missing"
        )
        Test.touch("restart_button")
        Test.wait(0.15)
        Test.expect(Session.observe().player.hp == 8, "Defeat restart did not restore health")
      end,
    },
    {
      name = "Player short sword deals melee damage without firing projectiles",
      func = function()
        Test.reset()
        Test.wait(0.1)
        Session.set_driver(function(state)
          local target = state.enemies[1]
          if target.hp <= 0 then
            return {}
          end
          local dx, dy, distance =
            Combat.direction(target.x - state.player.x, target.y - state.player.y)
          return {
            move_x = distance > 1.1 and dx or 0,
            move_y = distance > 1.1 and dy or 0,
            aim_x = target.x,
            aim_y = target.y + 0.55,
            attack = true,
            heal = state.player.hp <= 4,
          }
        end)
        for attempt = 1, 200 do
          if Session.observe().enemies[1].hp <= 0 then
            break
          end
          Test.wait(0.05)
        end
        Session.set_driver(nil)
        local state = Session.observe()
        Test.expect(state.enemies[1].hp == 0, "Short sword did not defeat the duelist")
        Test.expect(state.shots == 0, "Sword fired a magic projectile")
        Test.expect(state.player.hp > 0, "Melee encounter was not survivable")
      end,
    },
    require("tests.weapon_swap"),
    {
      name = "Magic cannot fire without mana and the desktop flask slot refills it",
      func = function()
        Test.reset()
        Session.set_driver(function()
          return { equip_slot = 4, attack = true, aim_x = -10, aim_y = -10, heal = true }
        end)
        Test.wait(3.5)
        Session.set_driver(nil)
        local before = Session.observe()
        Test.expect(
          before.player.mana == 0 and before.shots == 4,
          "Staff did not exhaust its finite reserve"
        )
        Test.touch("quick_frame_2")
        Test.wait(0.1)
        Test.expect(Session.observe().player.mana == 6, "Quick-slot mana flask did not refill mana")
        Test.expect(
          Inventory.count(Session.observe().player.inventory, "mana_potion") == 0,
          "Flask was not consumed"
        )
      end,
    },
    {
      name = "Discovery, native vault collision, journal, treasure and Continue",
      func = function()
        local Navigator = require("tests.navigation")
        local Physics = require("demi.physics.query2d")
        Test.reset()
        Test.expect(not Session.observe().discovered["3:24"], "Secret room was visible at spawn")
        Test.expect(
          Physics.raycast(16, 14, 1, 0, 5, "wall") == nil,
          "Water incorrectly blocks sight and spells"
        )
        Test.expect(
          Physics.raycast(16, 14, 1, 0, 5, "pit") ~= nil,
          "Water lost its movement barrier"
        )
        Navigator.travel(Test, Session, 8, 7)
        Test.touch("interact_button")
        Test.wait(0.1)
        Test.expect(#Session.observe().journal == 1, "Keeper note was not recorded")
        Test.touch("journal_button")
        Test.expect(Hud.get_text("bag_title") == "DISCOVERIES & NOTES", "Journal did not open")
        Test.touch("bag_close")
        Navigator.travel(Test, Session, 24, 6)
        Test.expect(
          Physics.raycast(24, 6, 1, 0, 3, "wall") ~= nil,
          "Closed vault gate has no native collision"
        )
        Navigator.travel(Test, Session, 22, 3)
        Test.touch("interact_button")
        Test.wait(0.1)
        Test.expect(
          Session.observe().gates[1].open and Session.observe().gates[2].open,
          "Linked gates did not open"
        )
        Test.expect(
          Physics.raycast(24, 6, 1, 0, 3, "wall") == nil,
          "Open gate retained native collision"
        )
        Navigator.travel(Test, Session, 30, 3)
        Test.touch("interact_button")
        Test.wait(0.1)
        Test.expect(
          Inventory.count(Session.observe().player.inventory, "frost_staff") == 1,
          "Vault reward was not delivered"
        )
        Test.touch("pause_button")
        Test.touch("title_button")
        Test.wait(0.1)
        Test.touch("continue_button")
        Test.wait(0.2)
        local restored = Session.observe()
        Test.expect(
          restored.gates[1].open and #restored.journal == 1,
          "Continue lost gate or journal state"
        )
        Test.expect(
          Inventory.count(restored.player.inventory, "frost_staff") == 1,
          "Continue lost the discovered staff"
        )
      end,
    },
    {
      name = "Stairs and separate floor scenes do not require clearing enemies",
      func = function()
        local Navigator = require("tests.navigation")
        local Floors = require("dungeon.floors")
        Test.reset()
        local nav = Navigator.new()
        Session.set_driver(function(state)
          local floor = Floors.create(state.floor)
          return nav:action(state, floor.exit[1], floor.exit[2], true)
        end)
        for attempt = 1, 600 do
          if Session.observe().phase == "upgrade" then
            break
          end
          Test.expect(Session.observe().phase ~= "dead", "Exit traversal died")
          Test.wait(0.05)
        end
        Session.set_driver(nil)
        local state = Session.observe()
        Test.expect(state.phase == "upgrade" and state.kills == 0, "Stairs still require combat")
        Test.expect(state.enemies[1].hp > 0, "Traversal silently cleared enemies")
        Test.touch("power_choice")
        Test.expect_scene("scene://dungeon/floor2")
        Test.wait(0.1)
        Test.expect(Session.observe().floor == 2, "Floor state did not transfer with its scene")
        Test.expect(
          Inventory.count(Session.observe().player.inventory, "oak_staff") == 1,
          "Scene transition lost inventory"
        )
      end,
    },
    {
      name = "Optional exits and mixed melee/magic can finish the expedition",
      func = function()
        Test.reset()
        Session.set_driver(Driver.new())
        for attempt = 1, 3600 do
          Test.wait(0.05)
          local state = Session.observe()
          Test.expect(state.phase ~= "dead", "Baseline died on floor " .. state.floor)
          if state.phase == "victory" then
            Session.set_driver(nil)
            Test.expect_scene("scene://dungeon/boss1")
            Test.wait(0.1)
            Test.expect(
              Hud.get_text("overlay_title") == "THE CROWN IS SILENT",
              "Result dialog missing"
            )
            Test.touch("restart_button")
            Test.expect_scene("scene://dungeon/floor1")
            Test.wait(0.1)
            Test.expect(Session.observe().player.hp == 8, "New expedition retained old state")
            return
          end
        end
        Session.set_driver(nil)
        Test.expect(false, "Expedition exceeded 180 seconds")
      end,
    },
  },
}
