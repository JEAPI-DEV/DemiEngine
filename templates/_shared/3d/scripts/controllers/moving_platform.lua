local MovingPlatform = {}

function MovingPlatform:on_create()
  local x, y, z = Transform3D.get_position(self.entity_id)
  self.origin_x, self.origin_y, self.origin_z = x, y, z
  self.distance = self.distance or 3.0
  self.speed = self.speed or 1.0
end

function MovingPlatform:on_fixed_update(dt)
  local x = self.origin_x + math.sin(Time.fixed_time * self.speed) * self.distance
  Rigidbody3D.move_kinematic(self.entity_id, x, self.origin_y, self.origin_z,
    0.0, 0.0, 0.0, dt)
end

return MovingPlatform
