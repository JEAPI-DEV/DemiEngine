-- Bounded exploration workload: real navigation/actions and enemy simulation.
-- Extra health keeps the unattended route alive; this is not a balance test.
local Session = require("dungeon.session")
local Navigator = require("tests.navigation")
local Profile = require("demi.profile")
local Application = require("demi.application")
local Input = require("demi.input")
local Probe = {}
local route = { {18, 5}, {18, 17}, {29, 17}, {40, 17}, {42, 35},
  {40, 17}, {29, 17}, {18, 17}, {5, 5} }

function Probe:on_update()
  local game = Session.game
  if not game then
    return
  end
  if not self.started then
    self.started = true
    self.waypoint = 1
    self.navigation = Navigator.new()
    game:start()
    game.run.player.max_hp = 100000
    game.run.player.hp = 100000
    Session.set_driver(function(state)
      if state.time < 5 or state.time >= 65 then
        return {}
      end
      local target = route[self.waypoint]
      if (state.player.x - target[1]) ^ 2 + (state.player.y - target[2]) ^ 2 < 0.45 ^ 2 then
        self.waypoint = self.waypoint % #route + 1
        target = route[self.waypoint]
      end
      local action
      Profile.scope("Bench.navigation_driver", function()
        action = self.navigation:action(state, target[1], target[2], true)
      end)
      return action
    end)
  end
  local time = game.run.time
  local phase = time < 2 and "warmup" or time < 5 and "stationary"
    or time < 65 and "moving" or "settled"
  if self.phase ~= phase then
    local width, height = Input.viewport_size()
    local discovered = 0
    for _ in pairs(game.run.discovered) do
      discovered = discovered + 1
    end
    print(string.format("BENCH viewport=%dx%d", width, height))
    print(string.format("BENCH phase=%s time=%.3f x=%.3f y=%.3f discovered=%d waypoint=%d",
      phase, time, game.run.player.x, game.run.player.y, discovered, self.waypoint))
  end
  self.phase = phase
  Profile.scope("Bench.dungeon." .. phase, function() end)
  if time > 70 then
    Application.quit()
  end
end
return Probe
