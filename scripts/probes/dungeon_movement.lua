local Session = require("dungeon.session")
local Profile = require("demi.profile")
local Input = require("demi.input")
local Application = require("demi.application")
local Probe = {}
function Probe:on_start()
  self.phase = "warmup"
end
function Probe:on_update()
  local game = Session.game
  if not game then
    return
  end
  if not self.started then
    self.started = true
    game:start()
    Session.set_driver(function(state)
      if state.time < 5 or state.time >= 11 then
        return {}
      end
      return { move_x = math.floor((state.time - 5) / 0.75) % 2 == 0 and 1 or -1 }
    end)
  end
  local time = game.run.time
  local phase = time < 2 and "warmup"
    or time < 5 and "stationary"
    or time < 11 and "moving"
    or "settled"
  if self.phase ~= phase then
    if phase == "stationary" then
      local width, height = Input.viewport_size()
      print(string.format("BENCH viewport=%dx%d", width, height))
    end
    print(
      string.format(
        "BENCH phase=%s time=%.4f x=%.4f y=%.4f",
        phase,
        time,
        game.run.player.x,
        game.run.player.y
      )
    )
  end
  self.phase = phase
  Profile.scope("Bench.dungeon." .. self.phase, function() end)
  if game.run.time > 14 then
    Application.quit()
  end
end
return Probe
