local Main = {}

function Main:on_create()
  self.speed = 3.0
end

function Main:on_fixed_update(dt)
  local x = Input.action_value("move_x")
  local y = Input.action_value("move_y")
  Transform.add_position(self.entity_id, x * self.speed * dt, y * self.speed * dt)
end

return Main
