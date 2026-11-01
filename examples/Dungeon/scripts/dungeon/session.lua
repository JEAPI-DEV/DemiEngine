-- Main-VM control boundary, shared by human input and future training drivers.
-- Drivers return the same action table as the keyboard/mouse adapter.
local Session = { game = nil, driver = nil }

function Session.observe()
  if not Session.game then
    return nil
  end
  local Run = require("dungeon.run")
  return Run.observe(Session.game.run)
end

function Session.set_driver(driver)
  assert(driver == nil or type(driver) == "function", "Driver must be a function")
  Session.driver = driver
end

return Session
