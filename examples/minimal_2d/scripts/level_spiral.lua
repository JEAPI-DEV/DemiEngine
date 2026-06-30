local state = require("game_state")

local Level = {
  generated_until_y = -2.6,
  next_spiral_index = 1,
  camera_x = 0.0,
  camera_y = 0.0,
}

local CAMERA_ID = "ent_camera_main"
local PLATFORM_HEIGHT = 0.45
local GENERATE_AHEAD_Y = 18.0

local SPIRAL_CENTER_X = 0.0
local SPIRAL_BASE_Y = -2.6
local SPIRAL_STEP_Y = 3.0
local SPIRAL_RADIUS_BASE = 4.0
local SPIRAL_RADIUS_GROW = 0.08
local SPIRAL_ANGLE_STEP = 1.5
local SPIRAL_WIDTH = 2.8
local SPIRAL_COIN_OFFSET = 2.2

function Level.reset()
  Level.generated_until_y = SPIRAL_BASE_Y
  Level.next_spiral_index = 1
  Level.camera_x = 0.0
  Level.camera_y = 0.0
end

function Level.on_start(game)
  Level.generate_ahead(game)
end

function Level.generate_ahead(game)
  while Level.generated_until_y < Level.camera_y + GENERATE_AHEAD_Y do
    local index = Level.next_spiral_index
    local angle = (index - 1) * SPIRAL_ANGLE_STEP
    local radius = SPIRAL_RADIUS_BASE + (index - 1) * SPIRAL_RADIUS_GROW
    local x = SPIRAL_CENTER_X + math.cos(angle) * radius
    local y = SPIRAL_BASE_Y + (index - 1) * SPIRAL_STEP_Y
    local id = "spiral_platform_" .. tostring(index)

    game:create_platform(id, "Spiral Platform " .. tostring(index), x, y, SPIRAL_WIDTH, PLATFORM_HEIGHT)

    if index % 3 == 0 then
      game:create_coin(x, y + SPIRAL_COIN_OFFSET)
    end

    Level.generated_until_y = y
    Level.next_spiral_index = index + 1
  end
end

function Level.update_camera(game, player_x, player_y)
  if state.camera_reset_requested then
    Level.camera_x = state.respawn_x
    Level.camera_y = state.respawn_y - 1.5
    state.camera_x = Level.camera_x
    state.camera_reset_requested = false
    Entity.set_position(CAMERA_ID, Level.camera_x, Level.camera_y)
    return
  end

  local target_x = player_x
  local target_y = player_y - 1.5
  Level.camera_x = Level.camera_x + (target_x - Level.camera_x) * 0.08
  Level.camera_y = Level.camera_y + (target_y - Level.camera_y) * 0.08

  state.camera_x = Level.camera_x
  Entity.set_position(CAMERA_ID, Level.camera_x, Level.camera_y)
end

function Level.on_update_track(game, player_x, player_y)
end

return Level
