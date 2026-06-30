local config = require("player_config")

local Slingshot = {}

local function clamp_pull(x, y)
  local length = math.sqrt((x * x) + (y * y))
  if length <= config.max_pull then
    return x, y
  end

  local scale = config.max_pull / length
  return x * scale, y * scale
end

local function draw_trajectory(origin_x, origin_y, velocity_x, velocity_y)
  local previous_x = origin_x
  local previous_y = origin_y
  local step = 0.12

  for i = 1, 18 do
    local t = i * step
    local next_x = origin_x + velocity_x * t
    local next_y = origin_y + velocity_y * t + 0.5 * config.trajectory_gravity * t * t
    Debug.line(previous_x, previous_y, next_x, next_y, 1.0, 0.92, 0.35, 1.0)
    previous_x = next_x
    previous_y = next_y
  end
end

function Slingshot.update_aim(player, player_x, player_y, grounded, mouse_down)
  if mouse_down and not player.mouse_was_down and grounded then
    player.aiming = true
  end

  if not player.aiming then
    return false
  end

  Rigidbody2D.set_velocity(player.entity_id, 0.0, 0.0)

  local mouse_x, mouse_y = Input.mouse_world_position()
  local pull_x, pull_y = clamp_pull(player_x - mouse_x, player_y - mouse_y)
  local launch_x = pull_x * config.sling_force
  local launch_y = pull_y * config.sling_force

  Debug.line(player_x, player_y, player_x - pull_x, player_y - pull_y, 0.9, 0.25, 0.25, 1.0)
  draw_trajectory(player_x, player_y, launch_x, launch_y)

  if not mouse_down then
    Rigidbody2D.set_velocity(player.entity_id, launch_x, launch_y)
    player.slingshot_active = true
    player.slingshot_frames = 0
    player.aiming = false
  end

  player.mouse_was_down = mouse_down
  return true
end

function Slingshot.update_active(player, grounded)
  if not player.slingshot_active then
    return
  end

  player.slingshot_frames = player.slingshot_frames + 1
  local velocity_x, velocity_y = Rigidbody2D.get_velocity(player.entity_id)
  local has_landed = grounded and player.slingshot_frames > 8 and velocity_y ~= nil and math.abs(velocity_y) < 0.01
  if has_landed then
    player.slingshot_active = false
    player.slingshot_frames = 0
  end
end

return Slingshot
