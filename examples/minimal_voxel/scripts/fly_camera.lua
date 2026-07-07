local terrain = require("worldgen.terrain")
local inventory = require("worldgen.inventory")
local state = require("worldgen.state")

local PlayerCamera = {}

local pause_hud_ids = {
  "pause_dim",
  "pause_panel",
  "pause_title",
  "pause_body",
  "pause_resume",
}

local hud_scale = 3

local function clamp(value, min_value, max_value)
  if value < min_value then
    return min_value
  end
  if value > max_value then
    return max_value
  end
  return value
end

local function set_pause_hud_visible(visible)
  for _, id in ipairs(pause_hud_ids) do
    Hud.set_visible(id, visible)
  end
end

local function move_vector(yaw, right, forward)
  local sin_y = math.sin(yaw)
  local cos_y = math.cos(yaw)
  return (right * cos_y) - (forward * sin_y), (-right * sin_y) - (forward * cos_y)
end

local function create_arm_part(entity_id, parent_id, position, rotation, size, color)
  Entity.create(entity_id, {
    name = entity_id,
    components = {
      Transform3D = {
        parent = parent_id,
        position = position,
        rotation = rotation,
        scale = { 1.0, 1.0, 1.0 },
      },
      MeshRenderer = {
        shape = "cube",
        size = size,
        color = color,
      },
    },
  })
end

local function player_collides(world, x, eye_y, z, radius, eye_height, body_height)
  local foot_y = eye_y - eye_height
  local min_x = math.floor(x - radius)
  local max_x = math.floor(x + radius)
  local min_y = math.floor(foot_y)
  local max_y = math.floor(foot_y + body_height)
  local min_z = math.floor(z - radius)
  local max_z = math.floor(z + radius)

  for by = min_y, max_y do
    for bz = min_z, max_z do
      for bx = min_x, max_x do
        if terrain.is_solid(terrain.block_at(world, bx, by, bz)) then
          return true
        end
      end
    end
  end
  return false
end

function PlayerCamera:set_paused(paused)
  self.paused = paused
  Runtime.set_mouse_captured(not paused)
  set_pause_hud_visible(paused)
end

function PlayerCamera:on_create()
  self.walk_speed = 6.0
  self.sprint_speed = 9.0
  self.fly_speed = 10.0
  self.fly_fast_speed = 24.0
  self.sensitivity = 0.003
  self.gravity = 28.0
  self.jump_speed = 8.5
  self.body_radius = 0.34
  self.body_height = 1.8
  self.eye_height = 1.62
  self.step_height = 0.6
  self.vertical_velocity = 0.0
  self.yaw = 0.0
  self.pitch = -0.35
  self.paused = false
  self.fly_mode = false
  self.grounded = false
  self.arm_bob = 0.0
  self.arm_swing = 0.0
  self.arm_sleeve_id = "ent_player_arm_sleeve"
  self.arm_hand_id = "ent_player_arm_hand"
  create_arm_part(
    self.arm_sleeve_id,
    self.entity_id,
    { 0.56, -0.72, -1.18 },
    { 0.95, -0.34, 0.16 },
    { 0.14, 0.42, 0.14 },
    { 0.28, 0.62, 0.22, 1.0 }
  )
  create_arm_part(
    self.arm_hand_id,
    self.entity_id,
    { 0.61, -0.98, -1.26 },
    { 0.95, -0.34, 0.16 },
    { 0.16, 0.16, 0.16 },
    { 0.75, 0.54, 0.36, 1.0 }
  )
  Hud.text("hud_hint", "WASD walk - Space jump - F fly - Mouse look - L/R click edit - ESC pause", 20.0, 20.0, hud_scale)
  Hud.text("hud_pos", "pos: (0.0, 0.0, 0.0)", 20.0, 68.0, hud_scale)
  Hud.rect("pause_dim", 0.0, 0.0, 960.0, 540.0, 0.02, 0.02, 0.03, 0.68)
  Hud.rect("pause_panel", 300.0, 176.0, 360.0, 188.0, 0.08, 0.09, 0.11, 0.94)
  Hud.text("pause_title", "PAUSED", 378.0, 214.0, 6.0, 0.94, 0.98, 1.0, 1.0)
  Hud.text("pause_body", "MOUSE IS FREE", 360.0, 284.0, 3.0, 0.78, 0.86, 0.92, 1.0)
  Hud.text("pause_resume", "ESC TO RESUME", 352.0, 322.0, 3.0, 1.0, 0.82, 0.44, 1.0)
  set_pause_hud_visible(false)
end

function PlayerCamera:update_arm(dt, moving)
  if Input.mouse_down("left") or Input.mouse_down("right") then
    self.arm_swing = 1.0
  end

  if moving then
    self.arm_bob = self.arm_bob + (dt * 9.0)
  else
    self.arm_bob = self.arm_bob * math.max(0.0, 1.0 - (dt * 7.0))
  end

  self.arm_swing = math.max(0.0, self.arm_swing - (dt * 4.0))
  local bob = math.sin(self.arm_bob) * (moving and 0.020 or 0.006)
  local sway = math.cos(self.arm_bob * 0.5) * (moving and 0.014 or 0.0)
  local swing = math.sin(self.arm_swing * math.pi)
  local pitch = 0.95 + (swing * 0.75)

  Transform3D.set_position(self.arm_sleeve_id, 0.56 + sway + (swing * 0.05), -0.72 + bob - (swing * 0.05), -1.18 - (swing * 0.08))
  Transform3D.set_rotation(self.arm_sleeve_id, pitch, -0.34, 0.16 - (swing * 0.10))
  Transform3D.set_position(self.arm_hand_id, 0.61 + sway + (swing * 0.08), -0.98 + bob - (swing * 0.08), -1.26 - (swing * 0.10))
  Transform3D.set_rotation(self.arm_hand_id, pitch, -0.34, 0.16 - (swing * 0.10))
end

function PlayerCamera:hide_arm()
  Transform3D.set_position(self.arm_sleeve_id, 0.0, -10000.0, 0.0)
  Transform3D.set_position(self.arm_hand_id, 0.0, -10000.0, 0.0)
end

function PlayerCamera:on_start()
  Runtime.set_mouse_captured(true)
  local world = state.get()
  local x, y, z = Transform3D.get_position(self.entity_id)
  if world ~= nil and x ~= nil and y ~= nil and z ~= nil then
    local ground_y = terrain.surface_height(world, math.floor(x), math.floor(z))
    if ground_y ~= nil and ground_y >= 0 then
      Transform3D.set_position(self.entity_id, x, ground_y + self.eye_height + 1.05, z)
    end
  end
end

function PlayerCamera:move_axis(world, dx, dy, dz)
  if dx == 0.0 and dy == 0.0 and dz == 0.0 then
    return true
  end

  local x, y, z = Transform3D.get_position(self.entity_id)
  local next_x = (x or 0.0) + dx
  local next_y = (y or 0.0) + dy
  local next_z = (z or 0.0) + dz
  if not player_collides(world, next_x, next_y, next_z, self.body_radius, self.eye_height, self.body_height) then
    Transform3D.set_position(self.entity_id, next_x, next_y, next_z)
    return true
  end
  return false
end

function PlayerCamera:move_horizontal(world, dx, dz)
  if self:move_axis(world, dx, 0.0, 0.0) then
    self:move_axis(world, 0.0, 0.0, dz)
    return
  end
  if self.grounded and self:move_axis(world, 0.0, self.step_height, 0.0) then
    if self:move_axis(world, dx, 0.0, 0.0) then
      self:move_axis(world, 0.0, 0.0, dz)
      return
    end
    self:move_axis(world, 0.0, -self.step_height, 0.0)
  end
  self:move_axis(world, 0.0, 0.0, dz)
end

function PlayerCamera:update_walk(world, dt)
  local right = Input.axis("a", "d")
  local forward = Input.axis("s", "w")
  local speed = Input.is_down("left ctrl") and self.sprint_speed or self.walk_speed
  local move_x, move_z = move_vector(self.yaw, right, forward)
  self:move_horizontal(world, move_x * speed * dt, move_z * speed * dt)

  if Input.is_pressed("space") and self.grounded then
    self.vertical_velocity = self.jump_speed
    self.grounded = false
  end

  self.vertical_velocity = self.vertical_velocity - (self.gravity * dt)
  local dy = self.vertical_velocity * dt
  if dy ~= 0.0 then
    local moved = self:move_axis(world, 0.0, dy, 0.0)
    if not moved then
      if dy < 0.0 then
        self.grounded = true
      end
      self.vertical_velocity = 0.0
    else
      self.grounded = false
    end
  end
end

function PlayerCamera:update_fly(dt)
  local right = Input.axis("a", "d")
  local forward = Input.axis("s", "w")
  local vertical = Input.axis("left shift", "space")
  local speed = Input.is_down("left ctrl") and self.fly_fast_speed or self.fly_speed
  local move_x, move_z = move_vector(self.yaw, right, forward)

  if right ~= 0.0 or forward ~= 0.0 or vertical ~= 0.0 then
    Transform3D.add_position(self.entity_id, move_x * speed * dt, vertical * speed * dt, move_z * speed * dt)
  end
end

function PlayerCamera:on_update(dt)
  if Input.is_pressed("escape") then
    self:set_paused(not self.paused)
  end

  if self.paused then
    self:hide_arm()
    return
  end

  local world = state.get()
  local inventory_open = inventory.is_open(world and world.inventory)
  if inventory_open then
    self:hide_arm()
    local x, y, z = Transform3D.get_position(self.entity_id)
    Hud.set_text(
      "hud_pos",
      string.format(
        "pos: (%.1f, %.1f, %.1f) %s",
        x or 0.0,
        y or 0.0,
        z or 0.0,
        self.fly_mode and "fly" or (self.grounded and "ground" or "air")
      )
    )
    return
  end

  if Input.is_pressed("f") then
    self.fly_mode = not self.fly_mode
    self.vertical_velocity = 0.0
    Hud.set_text(
      "hud_hint",
      self.fly_mode
        and "WASD fly - Space/Shift vertical - F walk - Mouse look - L/R click edit - ESC pause"
        or "WASD walk - Space jump - F fly - Mouse look - L/R click edit - ESC pause"
    )
  end

  local dx, dy = Input.mouse_delta()
  self.yaw = self.yaw - dx * self.sensitivity
  self.pitch = clamp(self.pitch - dy * self.sensitivity, -1.45, 1.45)
  Transform3D.set_rotation(self.entity_id, self.pitch, self.yaw, 0.0)

  local right = Input.axis("a", "d")
  local forward = Input.axis("s", "w")
  local vertical = self.fly_mode and Input.axis("left shift", "space") or 0.0
  local moving = right ~= 0.0 or forward ~= 0.0 or vertical ~= 0.0

  if self.fly_mode then
    self:update_fly(dt)
  else
    self:update_walk(world, dt)
  end
  self:update_arm(dt, moving)

  local x, y, z = Transform3D.get_position(self.entity_id)
  Hud.set_text(
    "hud_pos",
    string.format(
      "pos: (%.1f, %.1f, %.1f) %s",
      x or 0.0,
      y or 0.0,
      z or 0.0,
      self.fly_mode and "fly" or (self.grounded and "ground" or "air")
    )
  )
end

function PlayerCamera:on_destroy()
  Runtime.set_mouse_captured(false)
  Entity.destroy(self.arm_sleeve_id)
  Entity.destroy(self.arm_hand_id)
end

return PlayerCamera
