local state = require("game_state")

local Level = {
  generated_until_x = 8.0,
  next_platform_index = 1,
  last_platform_y = -3.0,
  camera_x = 0.0,
  camera_y = 0.0,
}

local CAMERA_ID = "ent_camera_main"
local PLATFORM_HEIGHT = 0.45
local GENERATE_AHEAD = 32.0

local function pseudo_random(index, salt)
  local value = math.sin(index * 12.9898 + salt * 78.233) * 43758.5453
  return value - math.floor(value)
end

local function platform_width(index)
  return 2.6 + ((index * 37) % 5) * 0.35
end

local function platform_gap(index)
  return 3.2 + ((index * 19) % 4) * 0.45
end

local function platform_y(index)
  local wave = math.sin(index * 1.37) * 1.15
  local step = ((index % 3) - 1) * 0.35
  return -2.8 + wave + step
end

function Level.reset()
  Level.generated_until_x = 8.0
  Level.next_platform_index = 1
  Level.last_platform_y = -3.0
  Level.camera_x = 0.0
  Level.camera_y = 0.0
end

function Level.on_start(game)
  Level.generate_ahead(game)
end

function Level.create_gap_coins(game, index, start_x, end_x, from_y, to_y)
  if index % 4 ~= 0 then
    return
  end

  local gap = end_x - start_x
  if gap < 2.2 then
    return
  end

  local t = 0.5
  local x = start_x + gap * t
  local base_y = from_y + (to_y - from_y) * t
  local arc = math.sin(t * math.pi) * (1.8 + pseudo_random(index, 12) * 1.15)
  game:create_coin(x, base_y + arc)
end

function Level.create_platform_coin(game, index, platform_x, platform_y)
  if index % 9 ~= 0 then
    return
  end

  local high_offset = 1.75 + pseudo_random(index, 23) * 1.25
  if not game:create_coin(platform_x, platform_y + high_offset) then
    game:create_coin(platform_x + 1.9, platform_y + high_offset + 0.75)
  end
end

function Level.generate_ahead(game)
  while Level.generated_until_x < Level.camera_x + GENERATE_AHEAD do
    local index = Level.next_platform_index
    local width = platform_width(index)
    local gap = platform_gap(index)
    local x = Level.generated_until_x + gap + width * 0.5
    local y = platform_y(index)
    local id = "platform_" .. tostring(index)
    local previous_platform_edge = Level.generated_until_x
    local platform_left_edge = x - width * 0.5

    game:create_platform(id, "Generated Platform " .. tostring(index), x, y, width, PLATFORM_HEIGHT)

    if index % 9 == 0 then
      Level.create_platform_coin(game, index, x, y)
    else
      Level.create_gap_coins(game, index, previous_platform_edge, platform_left_edge, Level.last_platform_y, y)
    end

    Level.generated_until_x = x + width * 0.5
    Level.last_platform_y = y
    Level.next_platform_index = index + 1
  end
end

function Level.update_camera(game, player_x, player_y)
  if state.camera_reset_requested then
    Level.camera_x = math.max(0.0, state.respawn_x - 1.5)
    Level.camera_y = 0.0
    state.camera_x = Level.camera_x
    state.camera_reset_requested = false
    Entity.set_position(CAMERA_ID, Level.camera_x, Level.camera_y)
    return
  end

  local target_x = math.max(0.0, player_x - 1.5)
  if target_x > Level.camera_x then
    Level.camera_x = Level.camera_x + (target_x - Level.camera_x) * 0.08
  end
  Level.camera_y = 0.0

  state.camera_x = Level.camera_x
  Entity.set_position(CAMERA_ID, Level.camera_x, Level.camera_y)
end

function Level.on_update_track(game, player_x, player_y)
end

return Level
