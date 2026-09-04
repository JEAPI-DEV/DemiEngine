local Script = require("demi.script")
local Character = {}

function Character:on_create()
  Script.bind(self)
  self.speed = self.speed or 5.0
  self.jump_speed = self.jump_speed or 7.0
  self.move_x, self.move_z = 0.0, 0.0
end

function Character:on_update()
  -- Normalized aliases; combine the move_3d preset axes directly.
  local x = Input.value("move_right")
  local z = Input.value("move_forward")
  local length = math.sqrt(x * x + z * z)
  if length > 1.0 then x, z = x / length, z / length end
  self.move_x, self.move_z = x * self.speed, -z * self.speed
  if Input.pressed("jump") then
    CharacterController3D.jump(self.entity_id, self.jump_speed)
  end
end

function Character:on_fixed_update()
  CharacterController3D.set_velocity(
    self.entity_id, self.move_x, 0.0, self.move_z)
end

function Character:on_destroy()
  Script.release(self)
end

-- Re-export Script helper methods so self:on/self:after work.
for key, value in pairs(Script) do
  if Character[key] == nil then
    Character[key] = value
  end
end

return Character
