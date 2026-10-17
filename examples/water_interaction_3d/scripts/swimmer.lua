---@demi_component
local Swimming = require("demi.gameplay.water.swimming")
local Transform = require("demi.transform3d")
local Swimmer = {}

function Swimmer:on_create()
  self.water = Swimming.new(self.entity_id, {speed = 2, linear_drag = 2, min_depth = 1})
end

function Swimmer:on_fixed_update(dt)
  local x = Transform.get_world_position(self.entity_id)
  self.water:fixed_update(dt, x < 20 and {1, 0, 0} or {0, 0, 0})
end

return Swimmer
