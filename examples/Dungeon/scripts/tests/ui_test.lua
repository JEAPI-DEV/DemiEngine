local Native = require("demi.test")
local Ui = require("dungeon.ui")
local Test = setmetatable({}, { __index = Native })
function Test.touch(id)
  Native.touch(Ui.id(id))
  -- Allow the clicked action and its conditional layout to publish before the next target.
  Native.wait(0.08)
end
function Test.node_center(id)
  return Native.node_center(Ui.id(id))
end
function Test.reset()
  local Session = require("dungeon.session")
  local state = Session.observe()
  if state.phase == "ready" then
    Test.touch("start_button")
  elseif state.phase == "dead" or state.phase == "victory" then
    Test.touch("restart_button")
  else
    if Session.game.menus.mode then
      Test.touch("bag_close")
    end
    if not Session.game.paused then
      Test.touch("pause_button")
    end
    Test.touch("reset_button")
  end
  Test.expect_scene("scene://dungeon/floor1")
  Test.wait(0.1)
end
return Test
