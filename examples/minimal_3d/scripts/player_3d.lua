local Player3D = {}

function Player3D:on_create()
  Debug.log("3D Player created. Move with WASD, jump with SPACE, inspect with F.")
  self.speed = 6.0
  self.rotation_speed = 1.5
  self.yaw = 0.0
  self.move_x = 0.0
  self.move_z = 0.0
  self.subscriptions = {
    Events.subscribe("physics3d_trigger_enter", function(contact)
      if contact.entity_id == self.entity_id and contact.other_entity_id == "ent_pickup" then
        Entity.destroy("ent_pickup")
        Hud.set_text("hud_label", "Pickup collected", 20.0, 60.0, 4.0)
      end
    end),
  }
  Hud.set_text("hud_hint", "WASD move - SPACE jump - Q/E rotate - F cast - ESC quit", 20.0, 20.0, 3.0)
end

function Player3D:on_start()
  Hud.set_text("hud_label", "Minimal 3D", 20.0, 60.0, 4.0)
end

function Player3D:on_update(dt)
  Debug.clear_lines()

  local right = Input.action_value("move_right")
  local forward = Input.action_value("move_forward")
  if right ~= 0.0 and forward ~= 0.0 then
    right = right * 0.70710678
    forward = forward * 0.70710678
  end

  if Input.action_down("rotate_left") then
    self.yaw = self.yaw + self.rotation_speed * dt
  end
  if Input.action_down("rotate_right") then
    self.yaw = self.yaw - self.rotation_speed * dt
  end
  Transform3D.set_rotation(self.entity_id, 0.0, self.yaw, 0.0)

  local yaw = self.yaw
  local sin_y = math.sin(yaw)
  local cos_y = math.cos(yaw)
  local move_x = (right * cos_y) - (forward * sin_y)
  local move_z = (-right * sin_y) - (forward * cos_y)

  self.move_x = move_x * self.speed
  self.move_z = move_z * self.speed

  local px2, py2, pz2 = Transform3D.get_position(self.entity_id)
  Hud.set_text("position/label", string.format("pos: (%.1f, %.1f, %.1f)", px2, py2, pz2), 20.0, 100.0, 2.5)

  if Input.action_pressed("inspect") then
    local hit = Physics3D.sphere_cast(px2, py2 + 0.5, pz2, 0.12,
      -sin_y, 0.0, -cos_y, 8.0, nil, self.entity_id)
    if hit then
      Hud.set_text("hud_label", "Inspecting " .. hit.entity_id, 20.0, 60.0, 4.0)
    else
      Hud.set_text("hud_label", "Nothing in range", 20.0, 60.0, 4.0)
    end
  end
end

function Player3D:on_fixed_update(dt)
  CharacterController3D.set_velocity(self.entity_id, self.move_x, 0.0, self.move_z)
  if Input.action_pressed("jump") then
    CharacterController3D.jump(self.entity_id, 7.0)
  end

  local platform_x = math.sin(Time.fixed_time * 0.8) * 2.0
  Rigidbody3D.move_kinematic("ent_moving_platform", platform_x, 0.35, 3.0,
    0.0, 0.0, 0.0, dt)
end

function Player3D:on_destroy()
  for _, subscription in ipairs(self.subscriptions) do
    Events.unsubscribe(subscription)
  end
  Debug.log("3D Player destroyed")
end

return Player3D
