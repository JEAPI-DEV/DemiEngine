local Config = require("shooter.config")

local Movement = {}

local function normalized(x, y)
  local length = math.sqrt(x * x + y * y)
  if length > 1.0 then
    return x / length, y / length
  end
  return x, y
end

function Movement.update(game)
  local keyboard_x = Input.action_value("move_x")
  local keyboard_y = Input.action_value("move_y")
  local x = keyboard_x
  local y = keyboard_y
  x, y = normalized(x, y)

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
