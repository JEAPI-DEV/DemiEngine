-- Optional visual-review entrypoint. Run from an isolated copy of this project
-- with scripts/tests/e2e.lua returning require("tests.visual_review").
local Test = require("tests.ui_test")
local Session = require("dungeon.session")
local Transform = require("demi.transform2d")
local Progression = require("dungeon.progression")
require("dungeon.persistence").slot = "hollow_crown_visual_review"

local function place(x, y)
  local game = Session.game
  game.run.player.x, game.run.player.y = x, y
  game.run.player.invulnerable = 100
  Transform.set_position(game.world:id("player"), x, y)
  Transform.set_position("dungeon_camera", x, y)
end

return {
  tests = {
    {
      name = "Visual review: pool, talents, statue entrance and inventory",
      func = function()
        Test.wait(0.3)
        Test.reset()
        Test.expect(Session.observe().phase == "playing", "Review did not start gameplay")
        Session.set_driver(function()
          return {}
        end)
        for _, enemy in ipairs(Session.game.run.enemies) do
          if not enemy.dormant then
            enemy.hp, enemy.rewarded = 0, true
          end
        end
        place(16, 12)
        Test.wait(0.8)
        print("REVIEW pool")
        Test.wait(3)
        Progression.award(Session.game.run, 48)
        Test.touch("character_button")
        print("REVIEW talents")
        Test.wait(3)
        Test.touch("talent_close")
        place(41, 30)
        Test.wait(3.2)
        print("REVIEW entrance")
        Test.wait(3)
        Test.touch("inventory_button")
        print("REVIEW inventory")
        Test.wait(3)
        Test.touch("bag_close")
      end,
    },
  },
}
