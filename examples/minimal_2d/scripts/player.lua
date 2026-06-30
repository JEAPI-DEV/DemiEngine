local Player = {}

local FALL_RESET_Y = -9.5
local SLING_FORCE = 6.5
local MAX_PULL = 2.8
local TRAJECTORY_GRAVITY = -18.0

function Player:on_create()
  Debug.log("Player created")
  self.jump_was_down = false
  self.mouse_was_down = false
  self.aiming = false
  self.slingshot_active = false
  self.slingshot_frames = 0
  self.spawn_x = 0.0
  self.spawn_y = -2.05
end

function Player:on_start()
  Debug.log("Drag with left mouse while grounded to slingshot. Press P for points.")
  local x, y = Entity.get_position(self.entity_id)
  if x ~= nil and y ~= nil then
    self.spawn_x = x
    self.spawn_y = y
  end
end

local function horizontal_axis()
  local x = Input.axis("a", "d")
  if x == 0.0 then
    x = Input.axis("left", "right")
  end
  return x
end

local function wants_jump()
  return Input.is_down("space") or Input.is_down("w") or Input.is_down("up")
end

local function is_grounded(entity_id)
  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return false
  end

  return Physics2D.overlap_box(x, y - 0.56, 0.78, 0.12, entity_id)
end

local function clamp_pull(x, y)
  local length = math.sqrt((x * x) + (y * y))
  if length <= MAX_PULL then
    return x, y
  end

  local scale = MAX_PULL / length
  return x * scale, y * scale
end

local function draw_trajectory(origin_x, origin_y, velocity_x, velocity_y)
  local previous_x = origin_x
  local previous_y = origin_y
  local step = 0.12

  for i = 1, 18 do
    local t = i * step
    local next_x = origin_x + velocity_x * t
    local next_y = origin_y + velocity_y * t + 0.5 * TRAJECTORY_GRAVITY * t * t
    Debug.line(previous_x, previous_y, next_x, next_y, 1.0, 0.92, 0.35, 1.0)
    previous_x = next_x
    previous_y = next_y
  end
end

function Player:reset_to_spawn_if_fallen()
  local x, y = Entity.get_position(self.entity_id)
  if y == nil or y > FALL_RESET_Y then
    return
  end

  Entity.set_position(self.entity_id, self.spawn_x, self.spawn_y)
  Rigidbody2D.set_velocity(self.entity_id, 0.0, 0.0)
  self.slingshot_active = false
  self.slingshot_frames = 0
end

function Player:on_update(dt)
  Debug.clear_lines()
  self:reset_to_spawn_if_fallen()

  local player_x, player_y = Entity.get_position(self.entity_id)
  if player_x == nil or player_y == nil then
    return
  end

  local mouse_down = Input.mouse_down("left")
  local grounded = is_grounded(self.entity_id)

  if mouse_down and not self.mouse_was_down and grounded then
    self.aiming = true
  end

  if self.aiming then
    Rigidbody2D.set_velocity(self.entity_id, 0.0, 0.0)

    local mouse_x, mouse_y = Input.mouse_world_position()
    local pull_x, pull_y = clamp_pull(player_x - mouse_x, player_y - mouse_y)
    local launch_x = pull_x * SLING_FORCE
    local launch_y = pull_y * SLING_FORCE

    Debug.line(player_x, player_y, player_x - pull_x, player_y - pull_y, 0.9, 0.25, 0.25, 1.0)
    draw_trajectory(player_x, player_y, launch_x, launch_y)

    if not mouse_down then
      Rigidbody2D.set_velocity(self.entity_id, launch_x, launch_y)
      self.slingshot_active = true
      self.slingshot_frames = 0
      self.aiming = false
    end

    self.mouse_was_down = mouse_down
    return
  end

  if self.slingshot_active then
    self.slingshot_frames = self.slingshot_frames + 1
    local velocity_x, velocity_y = Rigidbody2D.get_velocity(self.entity_id)
    local has_landed = grounded and self.slingshot_frames > 8 and velocity_y ~= nil and math.abs(velocity_y) < 0.01
    if has_landed then
      self.slingshot_active = false
      self.slingshot_frames = 0
    end
  end

  local x = horizontal_axis()
  if grounded and not self.slingshot_active then
    Rigidbody2D.set_velocity_x(self.entity_id, x * self.speed)
  end

  local jump_down = wants_jump()
  if jump_down and not self.jump_was_down and grounded then
    Rigidbody2D.set_velocity_y(self.entity_id, self.jump_speed)
  end

  self.jump_was_down = jump_down
  self.mouse_was_down = mouse_down
end

function Player:on_fixed_update(dt)
end

function Player:on_destroy()
  Debug.log("Player destroyed")
end

return Player
