local Actions = {}
local context = nil

function Actions.bind(value)
  context = value
end

-- @HandleAction("build_arrow")
function Actions.build_arrow()
  context.building.select("arrow")
end

-- @HandleAction("build_wizard")
function Actions.build_wizard()
  context.building.select("wizard")
end

-- @HandleAction("start_wave")
function Actions.start_wave()
  context.waves.start()
end

return Actions
