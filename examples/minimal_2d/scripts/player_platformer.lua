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

function Platformer.reset_to_spawn_if_fallen(player)
  local x, y = Entity.get_position(player.entity_id)
  if y == nil or y > config.fall_reset_y then
    return false
  end

  Entity.set_position(player.entity_id, state.respawn_x, state.respawn_y)
  Rigidbody2D.set_velocity(player.entity_id, 0.0, 0.0)
  player.slingshot_active = false
  player.slingshot_frames = 0
  player.aiming = false
  state.camera_reset_requested = true
  return true
end

return Platformer
