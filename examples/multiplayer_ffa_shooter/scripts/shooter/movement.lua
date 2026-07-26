local Config = require("shooter.config")

local Movement = {}

local function normalized(x, y)
  local length = math.sqrt(x * x + y * y)
  if length > 1.0 then
    return x / length, y / length
  end
  return x, y
end

local function clamp(value, minimum, maximum)
  return math.max(minimum, math.min(maximum, value))
end

function Movement.update(game, dt)
  local keyboard_x = Input.action_value("move_x")
  local keyboard_y = Input.action_value("move_y")
  local x = keyboard_x
  local y = keyboard_y
  if math.abs(x) + math.abs(y) < 0.01 then
    x = game.mobile_x
    y = game.mobile_y
  end
  x, y = normalized(x, y)

  local current_x, current_y = Transform.get_position(Config.player_entity)
  if current_x == nil then
    return
  end

  if math.abs(x) + math.abs(y) > 0.01 then
    game.facing_x = x
    game.facing_y = y
    Transform.set_rotation(Config.player_entity, math.atan(y, x))
  end

  Transform.set_position(
    Config.player_entity,
    clamp(current_x + x * Config.player_speed * dt, -Config.arena_half_width, Config.arena_half_width),
    clamp(current_y + y * Config.player_speed * dt, -Config.arena_half_height, Config.arena_half_height)
  )
end

return Movement
