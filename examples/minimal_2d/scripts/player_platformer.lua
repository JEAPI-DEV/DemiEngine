local config = require("player_config")

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
    return
  end

  Entity.set_position(player.entity_id, player.spawn_x, player.spawn_y)
  Rigidbody2D.set_velocity(player.entity_id, 0.0, 0.0)
  player.slingshot_active = false
  player.slingshot_frames = 0
end

return Platformer
