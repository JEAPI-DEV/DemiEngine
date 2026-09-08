local Character = {}

function Character:on_create()
  self.speed = self.speed or 5.0
  self.jump_speed = self.jump_speed or 7.0
  self.move_x, self.move_z = 0.0, 0.0
end

function Character:on_update()
  local x = Input.action_value("move_x")
  local z = Input.action_value("move_z")
  local length = math.sqrt(x * x + z * z)
  if length > 1.0 then x, z = x / length, z / length end
  self.move_x, self.move_z = x * self.speed, -z * self.speed
  if Input.action_pressed("jump") then
    CharacterController3D.jump(self.entity_id, self.jump_speed)
  end
end

function Character:on_fixed_update()
  CharacterController3D.set_velocity(
    self.entity_id, self.move_x, 0.0, self.move_z)
end

return Character
