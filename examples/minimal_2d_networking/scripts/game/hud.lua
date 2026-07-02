local config = require("player_config")
local state = require("game_state")

local HudView = {}

local EXTRA_JUMP_SLOT_IDS = { "extra_jump_slot_1", "extra_jump_slot_2", "extra_jump_slot_3" }
local EXTRA_JUMP_COIN_IDS = { "extra_jump_coin_1", "extra_jump_coin_2", "extra_jump_coin_3" }

function HudView.reset_fps(game)
  game.fps_accumulator = 0.0
  game.fps_frames = 0
end

function HudView.update_points(points)
  Hud.set_text("points", "POINTS: " .. tostring(points))
end

function HudView.update_extra_jumps(visible)
  local extra_jumps = state.extra_jumps or 0
  for i = 1, config.max_extra_jumps do
    Hud.set_visible(EXTRA_JUMP_SLOT_IDS[i], visible)
    Hud.set_visible(EXTRA_JUMP_COIN_IDS[i], visible and i <= extra_jumps)
  end
end

function HudView.update_fps(game, dt)
  game.fps_accumulator = game.fps_accumulator + dt
  game.fps_frames = game.fps_frames + 1
  if game.fps_accumulator < 0.25 then
    return
  end

  local fps = math.floor((game.fps_frames / game.fps_accumulator) + 0.5)
  Hud.set_text("fps", "FPS: " .. tostring(fps))
  HudView.reset_fps(game)
end

return HudView
