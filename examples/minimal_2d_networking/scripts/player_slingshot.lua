local config = require("player_config")
local state = require("game_state")

local Slingshot = {}

local function clamp_pull(x, y)
  local length = math.sqrt((x * x) + (y * y))
  if length <= config.max_pull then
    return x, y
  end

  local scale = config.max_pull / length
  return x * scale, y * scale
end

local function clamp_velocity(x, y)
  local max_speed = config.max_sling_speed or 24.0
  local speed = math.sqrt((x * x) + (y * y))
  if speed <= max_speed then
    return x, y
  end

  local scale = max_speed / speed
  return x * scale, y * scale
end

local function aim_base_velocity(player)
  if player.aiming_freezes_motion then
    return 0.0, 0.0
  end

  if player.aim_base_velocity_x ~= nil and player.aim_base_velocity_y ~= nil then
    return player.aim_base_velocity_x, player.aim_base_velocity_y
  end

  local velocity_x, velocity_y = Rigidbody2D.get_velocity(player.entity_id)
  return velocity_x or 0.0, velocity_y or 0.0
end

local function launch_velocity(base_x, base_y, impulse_x, impulse_y)
  local impulse_length_squared = (impulse_x * impulse_x) + (impulse_y * impulse_y)
  if impulse_length_squared <= 0.0001 then
    return clamp_velocity(base_x, base_y)
  end

  local matching_speed = ((base_x * impulse_x) + (base_y * impulse_y)) / impulse_length_squared
  if matching_speed <= 0.0 then
    return clamp_velocity(impulse_x, impulse_y)
  end

  return clamp_velocity(impulse_x + (impulse_x * matching_speed), impulse_y + (impulse_y * matching_speed))
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

function Slingshot.update_aim(player, player_x, player_y, can_slingshot, grounded, touching_platform, mouse_down)
  if mouse_down and not player.mouse_was_down and can_slingshot then
    player.aiming = true
    player.aiming_freezes_motion = grounded
    player.aim_base_velocity_x, player.aim_base_velocity_y = aim_base_velocity(player)
  end

  if not player.aiming then
    return false
  end

  if player.aiming_freezes_motion then
    Rigidbody2D.set_velocity(player.entity_id, 0.0, 0.0)
  end

  if touching_platform then
    player.can_slingshot = true
  end

  local mouse_x, mouse_y = Input.mouse_world_position()
  local pull_x, pull_y = clamp_pull(player_x - mouse_x, player_y - mouse_y)
  local base_x, base_y = aim_base_velocity(player)
  local launch_x, launch_y = launch_velocity(base_x, base_y, pull_x * config.sling_force, pull_y * config.sling_force)

  Debug.line(player_x, player_y, player_x - pull_x, player_y - pull_y, 0.9, 0.25, 0.25, 1.0)
  draw_trajectory(player_x, player_y, launch_x, launch_y)

  if not mouse_down then
    if not player.can_slingshot then
      state.extra_jumps = math.max((state.extra_jumps or 0) - 1, 0)
    end

    Rigidbody2D.set_velocity(player.entity_id, launch_x, launch_y)
    player.slingshot_active = true
    player.slingshot_frames = 0
    player.can_slingshot = false
    player.aiming = false
    player.aiming_freezes_motion = false
    player.aim_base_velocity_x = nil
    player.aim_base_velocity_y = nil
  end

  player.mouse_was_down = mouse_down
  return true
end

function Slingshot.update_recharge(player, touching_platform)
  if touching_platform and (not player.slingshot_active or player.slingshot_frames > 8) then
    player.can_slingshot = true
  end
end

function Slingshot.update_active(player, touching_platform)
  if not player.slingshot_active then
    return
  end

  player.slingshot_frames = player.slingshot_frames + 1
  local velocity_x, velocity_y = Rigidbody2D.get_velocity(player.entity_id)
  local has_landed = touching_platform and player.slingshot_frames > 8 and velocity_y ~= nil and math.abs(velocity_y) < 0.01
  if has_landed then
    player.slingshot_active = false
    player.slingshot_frames = 0
  end
end

return Slingshot
