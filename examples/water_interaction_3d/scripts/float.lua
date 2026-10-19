---@demi_component
local Buoyancy = require("demi.gameplay.water.buoyancy")
local Float = {}

function Float:on_create()
  self.water = Buoyancy.new(self.entity_id, {
    half_height = .5, density_ratio = 2, linear_drag = 4,
  })
end

function Float:on_fixed_update(dt)
  self.water:fixed_update(dt)
end

return Float
