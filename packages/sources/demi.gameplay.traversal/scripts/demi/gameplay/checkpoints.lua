local Checkpoints = {}
Checkpoints.__index = Checkpoints

function Checkpoints.new(events)
  return setmetatable({ events = assert(events), current = nil }, Checkpoints)
end

function Checkpoints:set(values)
  assert(type(values.id) == "string" and type(values.scene) == "string")
  self.current = { id = values.id, scene = values.scene, entrance = values.entrance,
    prefab = values.prefab, version = values.version or 1, data = values.data }
  self.events:emit("checkpoint_changed", self:save())
end

function Checkpoints:save()
  if not self.current then return nil end
  local copy = {}; for key, value in pairs(self.current) do copy[key] = value end
  return copy
end

function Checkpoints:load(value)
  if type(value) ~= "table" or type(value.id) ~= "string" or type(value.scene) ~= "string" then
    return false, "invalid_checkpoint"
  end
  self.current = value; return true
end

function Checkpoints:respawn()
  if not self.current then return false end
  self.events:emit("respawn_requested", self:save()); return true
end

function Checkpoints:enter(values)
  if type(values) ~= "table" or type(values.scene) ~= "string" or
    type(values.entrance) ~= "string" then return false, "invalid_entrance" end
  self.events:emit("scene_entrance_requested", {
    scene = values.scene,
    entrance = values.entrance,
    data = values.data,
  })
  return true
end

return Checkpoints
