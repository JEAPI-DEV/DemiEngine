local Actions = {}
local context = nil

function Actions.bind(value)
  context = value
end

-- @HandleAction("build_arrow")
function Actions.build_arrow()
  context.building.select("arrow")
end

-- @HandleAction("build_cannon")
function Actions.build_cannon()
  context.building.select("cannon")
end

-- @HandleAction("start_wave")
function Actions.start_wave()
  context.waves.start()
end

return Actions
