local Transform = require("demi.transform3d")

---@demi_component
local Moving = {}

function Moving:on_start()
  self.time = 0
end

function Moving:on_update(dt)
  self.time = self.time + dt
  Transform.set_position(self.entity_id, math.sin(self.time) * 2, 2.5, 0)
  Transform.set_rotation(self.entity_id, 0, self.time * 0.4, 0)
end

return Moving
