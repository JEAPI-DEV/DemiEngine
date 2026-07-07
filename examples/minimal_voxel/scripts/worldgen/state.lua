local State = {}

local current = nil

function State.set(world)
  current = world
end

function State.get()
  return current
end

return State
