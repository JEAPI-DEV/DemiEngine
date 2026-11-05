local Test = require("tests.ui_test")
local Session = require("dungeon.session")
local Progression = require("dungeon.progression")
local Transform = require("demi.transform2d")
local Checkpoint = require("dungeon.checkpoint")

local function place_player(x, y)
  local game = Session.game
  game.run.player.x, game.run.player.y = x, y
  Transform.set_position(game.world:id("player"), x, y)
end

return {
  name = "Pool collision, talent choices and skippable statue entrance",
  func = function()
    Test.reset()
    place_player(16, 14)
    Session.set_driver(function()
      return { move_x = 1 }
    end)
    Test.wait(0.5)
    Test.expect(Session.observe().player.x < 16.25, "Pool collider did not match its west rim")
    Session.set_driver(function()
      return {}
    end)
    Progression.award(Session.game.run, 48)
    Test.touch("character_button")
    Test.touch("talent_arcana")
    Test.touch("talent_arcana")
    local player = Session.observe().player
    Test.expect(player.max_mana == 12, "Two Arcana ranks did not increase mana capacity")
    Test.expect(player.talents.arcana == 2, "Talent buttons did not spend points")
    Test.touch("talent_close")
    place_player(41, 30)
    Test.wait(0.2)
    Test.expect(Session.game.run.cinematic ~= nil, "Room entry did not start the reveal")
    local health = Session.observe().player.hp
    Test.wait(1)
    Test.expect(Session.observe().player.hp == health, "Enemies attacked during the entrance")
    Session.set_driver(nil)
    Test.touch("interact_button")
    Test.wait(0.1)
    Test.expect(Session.game.run.cinematic == nil, "E interaction did not skip the entrance")
    local encounter = Session.game.run.floor.ambushes[1]
    for _, entry in ipairs(encounter.actors) do
      for _, enemy in ipairs(Session.observe().enemies) do
        if enemy.id == entry.id then
          Test.expect(not enemy.dormant and enemy.y < 34, "Guard failed to emerge past its statue")
        end
      end
    end
    local restored, error = Checkpoint.decode(Checkpoint.encode(Session.game.run))
    Test.expect(restored ~= nil, error or "Checkpoint failed")
    Test.expect(restored.player.talents.arcana == 2, "Save lost learned talents")
    Test.reset()
    Session.set_driver(nil)
  end,
}
