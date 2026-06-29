local Player = {}

local FALL_RESET_Y = -9.5

function Player:on_create()
  Debug.log("Player created")
  self.jump_was_down = false
  self.spawn_x = 0.0
  self.spawn_y = -2.05
end

function Player:on_start()
  Debug.log("Use A/D or arrows to move. Press Space/W/Up to jump.")
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

function Player:reset_to_spawn_if_fallen()
  local x, y = Entity.get_position(self.entity_id)
  if y == nil or y > FALL_RESET_Y then
    return
  end

  Entity.set_position(self.entity_id, self.spawn_x, self.spawn_y)
  Rigidbody2D.set_velocity(self.entity_id, 0.0, 0.0)
end

function Player:on_update(dt)
  self:reset_to_spawn_if_fallen()

  local x = horizontal_axis()
  Rigidbody2D.set_velocity_x(self.entity_id, x * self.speed)

  local jump_down = wants_jump()
  if jump_down and not self.jump_was_down and is_grounded(self.entity_id) then
    Rigidbody2D.set_velocity_y(self.entity_id, self.jump_speed)
  end

  self.jump_was_down = jump_down
end

function Player:on_fixed_update(dt)
end

function Player:on_destroy()
  Debug.log("Player destroyed")
end

return Player
