local config = require("player_config")
local state = require("game_state")
local platformer = require("player_platformer")
local slingshot = require("player_slingshot")

local Player = {}

function Player:on_create()
  Debug.log("Player created")
  self.jump_was_down = false
  self.mouse_was_down = false
  self.aiming = false
  self.slingshot_active = false
  self.slingshot_frames = 0
  self.spawn_x = config.spawn_x
  self.spawn_y = config.spawn_y
end

function Player:on_start()
  Debug.log("Drag with left mouse while grounded to slingshot. Press P for points.")
  local x, y = Entity.get_position(self.entity_id)
  if x ~= nil and y ~= nil then
    self.spawn_x = x
    self.spawn_y = y
    state.respawn_x = x
    state.respawn_y = y
  end
end

function Player:on_update(dt)
  Debug.clear_lines()
  platformer.reset_to_spawn_if_fallen(self)

  local player_x, player_y = Entity.get_position(self.entity_id)
  if player_x == nil or player_y == nil then
    return
  end

  local mouse_down = Input.mouse_down("left")
  local grounded = platformer.is_grounded(self.entity_id)

  if slingshot.update_aim(self, player_x, player_y, grounded, mouse_down) then
    return
  end

  slingshot.update_active(self, grounded)

  if grounded and not self.slingshot_active then
    Rigidbody2D.set_velocity_x(self.entity_id, platformer.horizontal_axis() * self.speed)
  end

  local jump_down = platformer.wants_jump()
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
