local Actions = {}
local game = nil

function Actions.bind(value)
  game = value
end

-- @HandleAction("host_match")
function Actions.host_match()
  if game ~= nil then game:host_match() end
end

-- @HandleAction("join_match")
function Actions.join_match()
  if game ~= nil then game:join_match() end
end

-- @HandleAction("practice_match")
function Actions.practice_match()
  if game ~= nil then game:practice_match() end
end

-- @HandleAction("move_up")
function Actions.move_up()
  if game ~= nil then game:set_mobile_direction(0, 1) end
end

-- @HandleAction("move_down")
function Actions.move_down()
  if game ~= nil then game:set_mobile_direction(0, -1) end
end

-- @HandleAction("move_left")
function Actions.move_left()
  if game ~= nil then game:set_mobile_direction(-1, 0) end
end

-- @HandleAction("move_right")
function Actions.move_right()
  if game ~= nil then game:set_mobile_direction(1, 0) end
end

-- @HandleAction("move_stop")
function Actions.move_stop()
  if game ~= nil then game:set_mobile_direction(0, 0) end
end

-- @HandleAction("fire_weapon")
function Actions.fire_weapon()
  if game ~= nil then game.fire_requested = true end
end

return Actions
