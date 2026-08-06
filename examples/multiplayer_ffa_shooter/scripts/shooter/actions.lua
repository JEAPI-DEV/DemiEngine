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

return Actions
