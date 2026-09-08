local Main = {}

function Main:on_create()
  self.speed = 3.0
end

function Main:on_fixed_update(dt)
  local x = Input.action_value("move_x")
  local z = Input.action_value("move_z")
  Transform3D.add_position(self.entity_id, x * self.speed * dt, 0.0, -z * self.speed * dt)
end

return Main
