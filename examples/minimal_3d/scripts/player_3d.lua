---@demi_component
---@display_name Player Controller 3D
---@category Gameplay
---@description First-person movement, jumping, rotation, and projectiles.
local Script = require("demi.script")
local Player3D = {}

---@demi_property
---@label Move Speed
---@range 0 20
Player3D.speed = 6.0

---@demi_property
---@label Rotation Speed
---@range 0 100
Player3D.rotation_speed = 1.5

---@demi_property
---@label Jump Buffer
---@range 0 10
Player3D.jump_buffer_duration = 0.12

---@demi_property
---@label Coyote Time
---@range 0 10
Player3D.coyote_duration = 0.12

function Player3D:on_create()
  Debug.log("3D Player created. Move with WASD, jump with SPACE, fire with F.")
  Script.bind(self)
  self.yaw = 0.0
  self.move_x = 0.0
  self.move_z = 0.0
  self.jump_buffer_remaining = 0.0
  self.coyote_remaining = 0.0
  self.next_projectile_id = 1
  self.music_restore_at = nil
  Audio.define_snapshot("exploration", { music = 1.0, sfx = 1.0 })
  Audio.define_snapshot("action", { music = 0.35, sfx = 1.0 })
  Audio.transition_snapshot("exploration", 0.0)
  -- Pickup: typed trigger helper replaces manual contact matching.
  local pickup_trigger = Physics.on_trigger(self.entity_id, function(contact)
    if contact.other_entity_id == "ent_pickup" then
      Entity.destroy("ent_pickup")
      Script.set_text(self, "hud_label", "Pickup collected")
    end
  end)
  self.pickup_trigger = pickup_trigger
  -- Projectiles: typed collision helper replaces the manual expiry sweep +
  -- contact table. Lifetime is owned by Script.spawn ttl below.
  local hit_2d, hit_3d = Physics.on_collision(self.entity_id, function(contact)
    for _, id in ipairs({ contact.entity_id, contact.other_entity_id }) do
      if id:find("^ent_projectile_") then
        Entity.destroy(id)
      end
    end
  end)
  self:on("physics3d_collision_enter", function(contact)
    for _, id in ipairs({ contact.entity_id, contact.other_entity_id }) do
      if id:find("^ent_projectile_") then
        Entity.destroy(id)
      end
    end
  end)
  self.projectile_hit = { hit_2d, hit_3d }
  self:set_text("hud_hint", "WASD move - SPACE jump - Q/E rotate - F/LMB fire - ESC quit")
end

function Player3D:on_start()
  self:set_text("hud_label", "Minimal 3D")
end

function Player3D:on_update(dt)
  Debug.clear_lines()

  -- Input.vector returns a diagonal-safe normalized vector (no 0.7071 hack).
  local right = Input.value("move_right")
  local forward = Input.value("move_forward")

  if Input.down("rotate_left") then
    self.yaw = self.yaw + self.rotation_speed * dt
  end
  if Input.down("rotate_right") then
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
  self:set_text("position/label", string.format("pos: (%.1f, %.1f, %.1f)", px2, py2, pz2))

  if Input.pressed("jump") then
    self.jump_buffer_remaining = self.jump_buffer_duration
  end

  if Input.pressed("fire") then
    Audio.transition_snapshot("action", 0.08)
    self.music_restore_at = Time.time + 0.35
    local projectile_id = "ent_projectile_" .. tostring(self.next_projectile_id)
    self.next_projectile_id = self.next_projectile_id + 1
    local direction_x = -sin_y
    local direction_z = -cos_y
    local created = Script.spawn(self, projectile_id, {
      position = {
        px2 + direction_x * 0.8,
        py2 + 0.65,
        pz2 + direction_z * 0.8,
      },
      velocity = {
        direction_x * 18.0,
        0.0,
        direction_z * 18.0,
      },
      ttl = 3.0,
      components = {
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
          use_gravity = false,
          mass = 0.1,
          continuous = true,
        },
      },
    })
    if created then
      self:set_text("hud_label", "Projectile fired")
    end
  end

  if self.music_restore_at and Time.time >= self.music_restore_at then
    Audio.transition_snapshot("exploration", 0.25)
    self.music_restore_at = nil
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

-- Re-export Script helper methods so self:move/set_text/on/after work.
for key, value in pairs(Script) do
  if Player3D[key] == nil then
    Player3D[key] = value
  end
end

function Player3D:on_destroy()
  Script.release(self)
  Debug.log("3D Player destroyed")
end

return Player3D
