local Config = require("shooter.config")

local Movement = {}

function Movement.update(game)
  -- Input aliases; gamepad/virtual bindings already normalize upstream.
  local x = Input.value("move_x")
  local y = Input.value("move_y")

  if math.abs(x) + math.abs(y) > 0.01 then
    game.facing_x = x
    game.facing_y = y
    Transform.set_rotation(Config.player_entity, math.atan(y, x))
  end

  Rigidbody2D.set_velocity(
    Config.player_entity,
    x * Config.player_speed,
    y * Config.player_speed
  )
end

return Movement
