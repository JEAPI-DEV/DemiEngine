local config = require("player_config")
local state = require("game_state")

local Platformer = {}

function Platformer.horizontal_axis()
  local x = Input.axis("a", "d")
  if x == 0.0 then
    x = Input.axis("left", "right")
  end
  return x
end

function Platformer.wants_jump()
  return Input.is_down("space") or Input.is_down("w") or Input.is_down("up")
end

function Platformer.is_grounded(entity_id)
  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return false
  end

  return Physics2D.overlap_box(x, y - 0.56, 0.78, 0.12, entity_id)
end

function Platformer.is_touching_platform(entity_id)
  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return false
  end

  return Physics2D.overlap_box(x, y - 0.50, 0.78, 0.10, entity_id)
end

function Platformer.check_game_over_if_fallen(player)
  local x, y = Entity.get_position(player.entity_id)
  if y == nil or y > config.fall_reset_y then
    return false
  end

  Rigidbody2D.set_velocity(player.entity_id, 0.0, 0.0)
  Audio.play("asset://audio/death")
  player.slingshot_active = false
  player.slingshot_frames = 0
  player.can_slingshot = false
  player.aiming = false
  player.aiming_freezes_motion = false
  state.game_over = true
  state.game_over_pending = true
  return true
end

return Platformer
