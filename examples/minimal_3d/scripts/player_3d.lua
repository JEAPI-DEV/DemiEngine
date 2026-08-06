local Player3D = {}

function Player3D:on_create()
  Debug.log("3D Player created. Move with WASD, jump with SPACE, fire with F.")
  self.speed = 6.0
  self.rotation_speed = 1.5
  self.yaw = 0.0
  self.move_x = 0.0
  self.move_z = 0.0
  self.jump_buffer_duration = 0.12
  self.coyote_duration = 0.12
  self.jump_buffer_remaining = 0.0
  self.coyote_remaining = 0.0
  self.next_projectile_id = 1
  self.projectiles = {}
  self.music_restore_at = nil
  Audio.define_snapshot("exploration", { music = 1.0, sfx = 1.0 })
  Audio.define_snapshot("action", { music = 0.35, sfx = 1.0 })
  Audio.transition_snapshot("exploration", 0.0)
  self.subscriptions = {
    Events.subscribe("physics3d_trigger_enter", function(contact)
      if contact.entity_id == self.entity_id and contact.other_entity_id == "ent_pickup" then
        Entity.destroy("ent_pickup")
        Hud.set_text("hud_label", "Pickup collected", 20.0, 60.0, 4.0)
      end
    end),
    Events.subscribe("physics3d_collision_enter", function(contact)
      local projectile_id = nil
      if self.projectiles[contact.entity_id] then
        projectile_id = contact.entity_id
      elseif self.projectiles[contact.other_entity_id] then
        projectile_id = contact.other_entity_id
      end
      if projectile_id then
        Entity.destroy(projectile_id)
        self.projectiles[projectile_id] = nil
      end
    end),
  }
  Hud.set_text("hud_hint", "WASD move - SPACE jump - Q/E rotate - F/LMB fire - ESC quit", 20.0, 20.0, 3.0)
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

  if Input.action_pressed("jump") then
    self.jump_buffer_remaining = self.jump_buffer_duration
  end

  if Input.action_pressed("fire") then
    Audio.transition_snapshot("action", 0.08)
    self.music_restore_at = Time.time + 0.35
    local projectile_id = "ent_projectile_" .. tostring(self.next_projectile_id)
    self.next_projectile_id = self.next_projectile_id + 1
    local direction_x = -sin_y
    local direction_z = -cos_y
    local created = Entity.create(projectile_id, {
      name = "Player projectile",
      components = {
        Transform3D = {
          position = {
            px2 + direction_x * 0.8,
            py2 + 0.65,
            pz2 + direction_z * 0.8,
          },
        },
        MeshRenderer = {
          shape = "sphere",
          size = { 0.28, 0.28, 0.28 },
          color = { 1.0, 0.35, 0.12, 1.0 },
        },
        SphereCollider3D = {
          radius = 0.14,
          layer = "projectile",
        },
        Rigidbody3D = {
          body_type = "dynamic",
          velocity = {
            direction_x * 18.0,
            0.0,
            direction_z * 18.0,
          },
          use_gravity = false,
          mass = 0.1,
          continuous = true,
        },
      },
    })
    if created then
      self.projectiles[projectile_id] = Time.time + 3.0
      Hud.set_text("hud_label", "Projectile fired", 20.0, 60.0, 4.0)
    end
  end

  if self.music_restore_at and Time.time >= self.music_restore_at then
    Audio.transition_snapshot("exploration", 0.25)
    self.music_restore_at = nil
  end

  for projectile_id, expires_at in pairs(self.projectiles) do
    if Time.time >= expires_at then
      Entity.destroy(projectile_id)
      self.projectiles[projectile_id] = nil
    end
  end
end

function Player3D:on_fixed_update(dt)
  CharacterController3D.set_velocity(self.entity_id, self.move_x, 0.0, self.move_z)

  local controller = CharacterController3D.state(self.entity_id)
  if controller and controller.grounded then
    self.coyote_remaining = self.coyote_duration
  else
    self.coyote_remaining = math.max(0.0, self.coyote_remaining - dt)
  end
  self.jump_buffer_remaining = math.max(0.0, self.jump_buffer_remaining - dt)
  if self.jump_buffer_remaining > 0.0 and self.coyote_remaining > 0.0 then
    CharacterController3D.jump(self.entity_id, 7.0)
    self.jump_buffer_remaining = 0.0
    self.coyote_remaining = 0.0
  end

  local platform_x = math.sin(Time.fixed_time * 0.8) * 2.0
  Rigidbody3D.move_kinematic("ent_moving_platform", platform_x, 0.35, 3.0,
    0.0, 0.0, 0.0, dt)
end

function Player3D:on_destroy()
  for projectile_id in pairs(self.projectiles) do
    Entity.destroy(projectile_id)
  end
  for _, subscription in ipairs(self.subscriptions) do
    Events.unsubscribe(subscription)
  end
  Debug.log("3D Player destroyed")
end

return Player3D
