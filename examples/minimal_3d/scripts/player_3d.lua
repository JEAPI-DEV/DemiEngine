local Player3D = {}

function Player3D:on_create()
  Debug.log("3D Player created. Move with WASD, rotate camera with Q/E.")
  self.speed = 6.0
  self.rotation_speed = 1.5
  self.yaw = 0.0
  Hud.set_text("hud_hint", "WASD move - Q/E rotate - ESC quit", 20.0, 20.0, 3.0)
end

function Player3D:on_start()
  Hud.set_text("hud_label", "Minimal 3D", 20.0, 60.0, 4.0)
end

function Player3D:on_update(dt)
  Debug.clear_lines()

  local x, z = 0.0, 0.0
  local right = Input.axis("a", "d")
  local forward = Input.axis("s", "w")
  if right ~= 0.0 and forward ~= 0.0 then
    right = right * 0.70710678
    forward = forward * 0.70710678
  end

  if Input.is_down("q") then
    self.yaw = self.yaw + self.rotation_speed * dt
  end
  if Input.is_down("e") then
    self.yaw = self.yaw - self.rotation_speed * dt
  end
  Transform3D.set_rotation(self.entity_id, 0.0, self.yaw, 0.0)

  local yaw = self.yaw
  local sin_y = math.sin(yaw)
  local cos_y = math.cos(yaw)
  local move_x = (right * cos_y) - (forward * sin_y)
  local move_z = (-right * sin_y) - (forward * cos_y)

  if move_x ~= 0.0 or move_z ~= 0.0 then
    Transform3D.add_position(self.entity_id, move_x * self.speed * dt, 0.0, move_z * self.speed * dt)
  end

  local px2, py2, pz2 = Transform3D.get_position(self.entity_id)
  Hud.set_text("hud_pos", string.format("pos: (%.1f, %.1f, %.1f)", px2, py2, pz2), 20.0, 100.0, 2.5)
end

function Player3D:on_fixed_update(dt)
end

function Player3D:on_destroy()
  Debug.log("3D Player destroyed")
end

return Player3D
