local config = require("player_config")
local state = require("game_state")
local platformer = require("player_platformer")
local slingshot = require("player_slingshot")
local replication = require("network_replication")
local player_colors = require("player_colors")
local CharacterController2D = require("demi.character_controller_2d")

local Player = {}

function Player:on_create()
  Debug.log("Player created")
  self.jump_was_down = false
  self.mouse_was_down = false
  self.aiming = false
  self.slingshot_active = false
  self.slingshot_frames = 0
  self.can_slingshot = false
  self.aiming_freezes_motion = false
  self.spawn_x = config.spawn_x
  self.spawn_y = config.spawn_y
  self.controller = CharacterController2D.new({
    move_speed = self.speed,
    jump_speed = self.jump_speed,
    ground_layer = "platform",
  })
end

function Player:on_start()
  Debug.log("Drag with left mouse after touching a platform to slingshot. Coins stack up to three extra airborne slingshots.")
  Debug.log("Networking demo active. Use the main menu Network Play screen to host or join by IP.")
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
  local color = player_colors.for_sender(replication.sender_id())
  Entity.set_sprite_color(self.entity_id, color[1], color[2], color[3], color[4])
  replication.set_local_color(color[1], color[2], color[3], color[4])

  if state.menu_open or state.game_over then
    self.jump_was_down = false
    self.mouse_was_down = Input.mouse_down("left")
    self.aiming = false
    self.aiming_freezes_motion = false
    self.aim_base_velocity_x = nil
    self.aim_base_velocity_y = nil
    return
  end

  if platformer.check_game_over_if_fallen(self) then
    return
  end

  local player_x, player_y = Entity.get_position(self.entity_id)
  if player_x == nil or player_y == nil then
    return
  end

  replication.update_local_transform(self.entity_id, dt)

  local mouse_down = Input.mouse_down("left")
  local grounded = self.controller:is_grounded(self.entity_id)
  local touching_platform = platformer.is_touching_platform(self.entity_id)

  slingshot.update_recharge(self, touching_platform)

  local can_slingshot = self.can_slingshot or (state.extra_jumps or 0) > 0
  if slingshot.update_aim(self, player_x, player_y, can_slingshot, grounded, touching_platform, mouse_down) then
    return
  end

  slingshot.update_active(self, touching_platform)

  if grounded and not self.slingshot_active then
    self.controller:update_horizontal(self.entity_id)
  end

  if self.controller:try_jump(self.entity_id, grounded) then
    self.can_slingshot = false
  end

  self.jump_was_down = Input.action_down("jump")
  self.mouse_was_down = mouse_down
end

function Player:on_fixed_update(dt)
end

function Player:on_destroy()
  Debug.log("Player destroyed")
end

return Player
